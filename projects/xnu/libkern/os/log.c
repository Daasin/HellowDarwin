/* * Copyright (c) 2019 Apple Inc. All rights reserved. */

#include <stddef.h>
#undef offset

#include <kern/cpu_data.h>
#include <os/base.h>
#include <os/object.h>
#include <os/log.h>
#include <stdbool.h>
#include <stdint.h>

#include <vm/vm_kern.h>
#include <mach/vm_statistics.h>
#include <kern/debug.h>
#include <libkern/libkern.h>
#include <libkern/kernel_mach_header.h>
#include <pexpert/pexpert.h>
#include <uuid/uuid.h>
#include <sys/msgbuf.h>

#include <mach/mach_time.h>
#include <kern/thread.h>
#include <kern/simple_lock.h>
#include <kern/kalloc.h>
#include <kern/clock.h>
#include <kern/assert.h>
#include <kern/startup.h>
#include <kern/task.h>

#define OS_FIREHOSE_SPI 1
#include <firehose/tracepoint_private.h>
#include <firehose/chunk_private.h>
#include <os/firehose_buffer_private.h>
#include <os/firehose.h>

#include <os/log_private.h>
#include "trace_internal.h"

#include "log_encode.h"
#include "log_mem.h"

struct os_log_s {
	int a;
};

struct os_log_s _os_log_default;
struct os_log_s _os_log_replay;

LOGMEM_STATIC_INIT(os_log_mem, 14, 9, 10);

extern vm_offset_t kernel_firehose_addr;
extern firehose_chunk_t firehose_boot_chunk;

extern bool bsd_log_lock(bool);
extern void bsd_log_unlock(void);
extern void logwakeup(struct msgbuf *);

decl_lck_spin_data(extern, oslog_stream_lock);
#define stream_lock() lck_spin_lock(&oslog_stream_lock)
#define stream_unlock() lck_spin_unlock(&oslog_stream_lock)

extern void oslog_streamwakeup(void);
void oslog_streamwrite_locked(firehose_tracepoint_id_u ftid,
    uint64_t stamp, const void *pubdata, size_t publen);
extern void oslog_streamwrite_metadata_locked(oslog_stream_buf_entry_t m_entry);

extern int oslog_stream_open;

extern void *OSKextKextForAddress(const void *);

/* Counters for persistence mode */
SCALABLE_COUNTER_DEFINE(oslog_p_total_msgcount);
SCALABLE_COUNTER_DEFINE(oslog_p_metadata_saved_msgcount);
SCALABLE_COUNTER_DEFINE(oslog_p_metadata_dropped_msgcount);
SCALABLE_COUNTER_DEFINE(oslog_p_error_count);
SCALABLE_COUNTER_DEFINE(oslog_p_saved_msgcount);
SCALABLE_COUNTER_DEFINE(oslog_p_dropped_msgcount);
SCALABLE_COUNTER_DEFINE(oslog_p_boot_dropped_msgcount);
SCALABLE_COUNTER_DEFINE(oslog_p_coprocessor_total_msgcount);
SCALABLE_COUNTER_DEFINE(oslog_p_coprocessor_dropped_msgcount);
SCALABLE_COUNTER_DEFINE(oslog_p_unresolved_kc_msgcount);

/* Counters for streaming mode */
SCALABLE_COUNTER_DEFINE(oslog_s_error_count);
/* Protected by the stream lock */
uint32_t oslog_s_total_msgcount;
uint32_t oslog_s_metadata_msgcount;

/* Counters for msgbuf logging */
SCALABLE_COUNTER_DEFINE(oslog_msgbuf_msgcount)
SCALABLE_COUNTER_DEFINE(oslog_msgbuf_dropped_msgcount)

static bool oslog_boot_done = false;

#ifdef XNU_KERNEL_PRIVATE
bool startup_serial_logging_active = true;
uint64_t startup_serial_num_procs = 300;
#endif /* XNU_KERNEL_PRIVATE */

// XXX
firehose_tracepoint_id_t
firehose_debug_trace(firehose_stream_t stream, firehose_tracepoint_id_t trace_id,
    uint64_t timestamp, const char *format, const void *pubdata, size_t publen);

static inline firehose_tracepoint_id_t
_firehose_trace(firehose_stream_t stream, firehose_tracepoint_id_u ftid,
    uint64_t stamp, const void *pubdata, size_t publen, bool use_streaming);

static oslog_stream_buf_entry_t
oslog_stream_create_buf_entry(oslog_stream_link_type_t type, firehose_tracepoint_id_u ftid,
    uint64_t stamp, const void* pubdata, size_t publen);

static void
_os_log_with_args_internal(os_log_t oslog __unused, os_log_type_t type __unused,
    const char *format, va_list args, void *addr, void *dso, bool driverKit, bool addcr);

static void
_os_log_to_msgbuf_internal(const char *format, va_list args, bool safe, bool logging, bool addcr);

static void
_os_log_to_log_internal(os_log_type_t type, const char *format, va_list args, void *addr, void *dso, bool driverKit);

static bool
os_log_turned_off(void)
{
	return atm_get_diagnostic_config() & (ATM_TRACE_DISABLE | ATM_TRACE_OFF);
}

bool
os_log_info_enabled(os_log_t log __unused)
{
	return !os_log_turned_off();
}

bool
os_log_debug_enabled(os_log_t log __unused)
{
	return !os_log_turned_off();
}

static bool
os_log_disabled(void)
{
	return atm_get_diagnostic_config() & ATM_TRACE_DISABLE;
}

os_log_t
os_log_create(const char *subsystem __unused, const char *category __unused)
{
	return &_os_log_default;
}

__attribute__((noinline, not_tail_called)) void
_os_log_internal(void *dso, os_log_t log, uint8_t type, const char *message, ...)
{
	va_list args;
	void *addr = __builtin_return_address(0);

	va_start(args, message);

	_os_log_with_args_internal(log, type, message, args, addr, dso, FALSE, FALSE);

	va_end(args);

	return;
}

__attribute__((noinline, not_tail_called)) int
_os_log_internal_driverKit(void *dso, os_log_t log, uint8_t type, const char *message, ...)
{
	va_list args;
	void *addr = __builtin_return_address(0);
	bool driverKitLog = FALSE;

	/*
	 * We want to be able to identify dexts from the logs.
	 *
	 * Usually the addr is used to understand if the log line
	 * was generated by a kext or the kernel main executable.
	 * Logd uses copyKextUUIDForAddress with the addr specified
	 * in the log line to retrieve the kext UUID of the sender.
	 *
	 * Dext however are not loaded in kernel space so they do not
	 * have a kernel range of addresses.
	 *
	 * To make the same mechanism work, OSKext fakes a kernel
	 * address range for dexts using the loadTag,
	 * so we just need to use the loadTag as addr here
	 * to allow logd to retrieve the correct UUID.
	 *
	 * NOTE: loadTag is populated in the task when the dext is matching,
	 * so if log lines are generated before the matching they will be
	 * identified as kernel main executable.
	 */
	task_t self_task = current_task();

	/*
	 * Only dextis are supposed to use this log path.
	 */
	if (!task_is_driver(self_task)) {
		return EPERM;
	}

	uint64_t loadTag = get_task_loadTag(self_task);
	if (loadTag != 0) {
		driverKitLog = TRUE;
		addr = (void*) loadTag;
	}
	va_start(args, message);

	_os_log_with_args_internal(log, type, message, args, addr, dso, driverKitLog, true);

	va_end(args);

	return 0;
}

#pragma mark - shim functions

__attribute__((noinline, not_tail_called)) void
os_log_with_args(os_log_t oslog, os_log_type_t type, const char *format, va_list args, void *addr)
{
	// if no address passed, look it up
	if (addr == NULL) {
		addr = __builtin_return_address(0);
	}

	_os_log_with_args_internal(oslog, type, format, args, addr, NULL, FALSE, FALSE);
}

static void
_os_log_with_args_internal(os_log_t oslog, os_log_type_t type,
    const char *format, va_list args, void *addr, void *dso, bool driverKit, bool addcr)
{
	if (format[0] == '\0') {
		return;
	}

	/* early boot can log to dmesg for later replay (27307943) */
	bool safe = (startup_phase < STARTUP_SUB_EARLY_BOOT || oslog_is_safe());
	bool logging = !os_log_turned_off();

	if (oslog != &_os_log_replay) {
		_os_log_to_msgbuf_internal(format, args, safe, logging, addcr);
	}

	if (safe && logging) {
		_os_log_to_log_internal(type, format, args, addr, dso, driverKit);
	}
}

static void
_os_log_to_msgbuf_internal(const char *format, va_list args, bool safe, bool logging, bool addcr)
{
	/*
	 * The following threshold was determined empirically as the point where
	 * it would be more advantageous to be able to fit in more log lines than
	 * to know exactly when a log line was printed out. We don't want to use up
	 * a large percentage of the log buffer on timestamps in a memory-constricted
	 * environment.
	 */
	const int MSGBUF_TIMESTAMP_THRESHOLD = 4096;
	static int msgbufreplay = -1;
	static bool newlogline = true;
	va_list args_copy;

	if (!bsd_log_lock(safe)) {
		counter_inc(&oslog_msgbuf_dropped_msgcount);
		return;
	}

	if (!safe) {
		if (-1 == msgbufreplay) {
			msgbufreplay = msgbufp->msg_bufx;
		}
	} else if (logging && (-1 != msgbufreplay)) {
		uint32_t i;
		uint32_t localbuff_size;
		int newl, position;
		char *localbuff, *p, *s, *next, ch;

		position = msgbufreplay;
		msgbufreplay = -1;
		localbuff_size = (msgbufp->msg_size + 2); /* + '\n' + '\0' */
		/* Size for non-blocking */
		if (localbuff_size > 4096) {
			localbuff_size = 4096;
		}
		bsd_log_unlock();
		/* Allocate a temporary non-circular buffer */
		localbuff = kheap_alloc(KHEAP_TEMP, localbuff_size, Z_NOWAIT);
		if (localbuff != NULL) {
			/* in between here, the log could become bigger, but that's fine */
			bsd_log_lock(true);
			/*
			 * The message buffer is circular; start at the replay pointer, and
			 * make one loop up to write pointer - 1.
			 */
			p = msgbufp->msg_bufc + position;
			for (i = newl = 0; p != msgbufp->msg_bufc + msgbufp->msg_bufx - 1; ++p) {
				if (p >= msgbufp->msg_bufc + msgbufp->msg_size) {
					p = msgbufp->msg_bufc;
				}
				ch = *p;
				if (ch == '\0') {
					continue;
				}
				newl = (ch == '\n');
				localbuff[i++] = ch;
				if (i >= (localbuff_size - 2)) {
					break;
				}
			}
			bsd_log_unlock();

			if (!newl) {
				localbuff[i++] = '\n';
			}
			localbuff[i++] = 0;

			s = localbuff;
			while ((next = strchr(s, '\n'))) {
				next++;
				ch = next[0];
				next[0] = 0;
				os_log(&_os_log_replay, "%s", s);
				next[0] = ch;
				s = next;
			}
			kheap_free(KHEAP_TEMP, localbuff, localbuff_size);
		}
		bsd_log_lock(true);
	}

	/* Do not prepend timestamps when we are memory-constricted */
	if (newlogline && (msgbufp->msg_size > MSGBUF_TIMESTAMP_THRESHOLD)) {
		clock_sec_t secs;
		clock_usec_t microsecs;
		const uint64_t timestamp = firehose_tracepoint_time(firehose_activity_flags_default);
		absolutetime_to_microtime(timestamp, &secs, &microsecs);
		printf_log_locked(FALSE, "[%5lu.%06u]: ", (unsigned long)secs, microsecs);
	}

	va_copy(args_copy, args);
	newlogline = vprintf_log_locked(format, args_copy, addcr);
	va_end(args_copy);

	bsd_log_unlock();
	logwakeup(msgbufp);
	counter_inc(&oslog_msgbuf_msgcount);
}

static firehose_stream_t
firehose_stream(os_log_type_t type)
{
	return (type == OS_LOG_TYPE_INFO || type == OS_LOG_TYPE_DEBUG) ?
	       firehose_stream_memory : firehose_stream_persist;
}

static void
_os_log_actual(os_log_type_t type, const char *format, void *dso, void *addr, uint8_t *logdata, size_t logdata_sz,
    firehose_tracepoint_flags_t flags, bool driverKit)
{
	firehose_tracepoint_id_u trace_id;

	firehose_stream_t stream = firehose_stream(type);
	uint64_t timestamp = firehose_tracepoint_time(firehose_activity_flags_default);

	if (driverKit) {
		// set FIREHOSE_TRACEPOINT_PC_DYNAMIC_BIT so logd will not try to find the format string in
		// the executable text
		trace_id.ftid_value = FIREHOSE_TRACE_ID_MAKE(firehose_tracepoint_namespace_log,
		    type, flags, (uint32_t)((uintptr_t)addr | FIREHOSE_TRACEPOINT_PC_DYNAMIC_BIT));
	} else {
		// create trace_id after we've set additional flags
		trace_id.ftid_value = FIREHOSE_TRACE_ID_MAKE(firehose_tracepoint_namespace_log,
		    type, flags, _os_trace_offset(dso, format, (_firehose_tracepoint_flags_activity_t)flags));
	}


	_firehose_trace(stream, trace_id, timestamp, logdata, logdata_sz, true);
}

static void *
resolve_dso(const char *fmt, void *dso, void *addr, bool driverKit)
{
	kc_format_t kcformat = KCFormatUnknown;

	if (!PE_get_primary_kc_format(&kcformat)) {
		return NULL;
	}

	switch (kcformat) {
	case KCFormatStatic:
	case KCFormatKCGEN:
		dso = PE_get_kc_baseaddress(KCKindPrimary);
		break;
	case KCFormatDynamic:
	case KCFormatFileset:
		if (!dso && (dso = (void *)OSKextKextForAddress(fmt)) == NULL) {
			return NULL;
		}
		if (!_os_trace_addr_in_text_segment(dso, fmt)) {
			return NULL;
		}
		if (!driverKit && (dso != (void *)OSKextKextForAddress(addr))) {
			return NULL;
		}
		break;
	default:
		panic("unknown KC format type");
	}

	return dso;
}

static void
_os_log_to_log_internal(os_log_type_t type, const char *fmt, va_list args, void *addr, void *dso, bool driverKit)
{
	counter_inc(&oslog_p_total_msgcount);

	if (addr == NULL) {
		counter_inc(&oslog_p_unresolved_kc_msgcount);
		return;
	}

	if ((dso = resolve_dso(fmt, dso, addr, driverKit)) == NULL) {
		counter_inc(&oslog_p_unresolved_kc_msgcount);
		return;
	}

	uint8_t buffer[OS_LOG_BUFFER_MAX_SIZE] __attribute__((aligned(8))) = { 0 };
	struct os_log_context_s ctx;

	os_log_context_init(&ctx, &os_log_mem, buffer, sizeof(buffer));

	if (os_log_context_encode(&ctx, fmt, args, addr, dso, driverKit)) {
		_os_log_actual(type, fmt, dso, addr, ctx.ctx_buffer, ctx.ctx_content_sz,
		    ctx.ctx_ft_flags, driverKit);
	} else {
		counter_inc(&oslog_p_error_count);
	}

	os_log_context_free(&ctx);
}

bool
os_log_coprocessor(void *buff, uint64_t buff_len, os_log_type_t type,
    const char *uuid, uint64_t timestamp, uint32_t offset, bool stream_log)
{
	firehose_tracepoint_id_u trace_id;
	firehose_tracepoint_id_t return_id = 0;
	uint8_t                  pubdata[OS_LOG_BUFFER_MAX_SIZE];
	size_t                   wr_pos = 0;

	if (os_log_turned_off()) {
		return false;
	}

	if (buff_len + 16 + sizeof(uint32_t) > OS_LOG_BUFFER_MAX_SIZE) {
		return false;
	}

	firehose_stream_t stream = firehose_stream(type);
	// unlike kext, where pc is used to find uuid, in coprocessor logs the uuid is passed as part of the tracepoint
	firehose_tracepoint_flags_t flags = _firehose_tracepoint_flags_pc_style_uuid_relative;

	memcpy(pubdata, &offset, sizeof(uint32_t));
	wr_pos += sizeof(uint32_t);
	memcpy(pubdata + wr_pos, uuid, 16);
	wr_pos += 16;

	memcpy(pubdata + wr_pos, buff, buff_len);

	// create firehose trace id
	trace_id.ftid_value = FIREHOSE_TRACE_ID_MAKE(firehose_tracepoint_namespace_log,
	    type, flags, offset);

	counter_inc(&oslog_p_coprocessor_total_msgcount);

	// send firehose tracepoint containing os log to firehose buffer
	return_id = _firehose_trace(stream, trace_id, timestamp, pubdata,
	    buff_len + wr_pos, stream_log);

	if (return_id == 0) {
		counter_inc(&oslog_p_coprocessor_dropped_msgcount);
		return false;
	}
	return true;
}

static inline firehose_tracepoint_id_t
_firehose_trace(firehose_stream_t stream, firehose_tracepoint_id_u ftid,
    uint64_t stamp, const void *pubdata, size_t publen, bool use_streaming)
{
	const uint16_t ft_size = offsetof(struct firehose_tracepoint_s, ft_data);
	const size_t _firehose_chunk_payload_size =
	    sizeof(((struct firehose_chunk_s *)0)->fc_data);

	firehose_tracepoint_t ft;

	if (slowpath(ft_size + publen > _firehose_chunk_payload_size)) {
		// We'll need to have some handling here. For now - return 0
		counter_inc(&oslog_p_error_count);
		return 0;
	}

	if (oslog_stream_open && (stream != firehose_stream_metadata) && use_streaming) {
		stream_lock();
		if (!oslog_stream_open) {
			stream_unlock();
			goto out;
		}

		oslog_s_total_msgcount++;
		oslog_streamwrite_locked(ftid, stamp, pubdata, publen);
		stream_unlock();
		oslog_streamwakeup();
	}

out:
	ft = __firehose_buffer_tracepoint_reserve(stamp, stream, (uint16_t)publen, 0, NULL);
	if (!fastpath(ft)) {
		if (oslog_boot_done) {
			if (stream == firehose_stream_metadata) {
				counter_inc(&oslog_p_metadata_dropped_msgcount);
			} else {
				// If we run out of space in the persistence buffer we're
				// dropping the message.
				counter_inc(&oslog_p_dropped_msgcount);
			}
			return 0;
		}
		firehose_chunk_t fbc = firehose_boot_chunk;
		long offset;

		//only stream available during boot is persist
		offset = firehose_chunk_tracepoint_try_reserve(fbc, stamp,
		    firehose_stream_persist, 0, (uint16_t)publen, 0, NULL);
		if (offset <= 0) {
			counter_inc(&oslog_p_boot_dropped_msgcount);
			return 0;
		}

		ft = firehose_chunk_tracepoint_begin(fbc, stamp, (uint16_t)publen,
		    thread_tid(current_thread()), offset);
		memcpy(ft->ft_data, pubdata, publen);
		firehose_chunk_tracepoint_end(fbc, ft, ftid);
		counter_inc(&oslog_p_saved_msgcount);
		return ftid.ftid_value;
	}
	if (!oslog_boot_done) {
		oslog_boot_done = true;
	}
	memcpy(ft->ft_data, pubdata, publen);

	__firehose_buffer_tracepoint_flush(ft, ftid);
	if (stream == firehose_stream_metadata) {
		counter_inc(&oslog_p_metadata_saved_msgcount);
	} else {
		counter_inc(&oslog_p_saved_msgcount);
	}
	return ftid.ftid_value;
}

static oslog_stream_buf_entry_t
oslog_stream_create_buf_entry(oslog_stream_link_type_t type,
    firehose_tracepoint_id_u ftid, uint64_t stamp,
    const void* pubdata, size_t publen)
{
	const size_t ft_size = sizeof(struct firehose_tracepoint_s) + publen;
	const size_t m_entry_len = sizeof(struct oslog_stream_buf_entry_s) + ft_size;
	oslog_stream_buf_entry_t m_entry;

	if (!pubdata || publen > UINT16_MAX || ft_size > UINT16_MAX) {
		return NULL;
	}

	m_entry = kheap_alloc(KHEAP_DEFAULT, m_entry_len, Z_WAITOK);
	if (!m_entry) {
		return NULL;
	}
	m_entry->type = type;
	m_entry->timestamp = stamp;
	m_entry->size = (uint16_t)ft_size;

	firehose_tracepoint_t ft = m_entry->metadata;
	ft->ft_thread = thread_tid(current_thread());
	ft->ft_id.ftid_value = ftid.ftid_value;
	ft->ft_length = (uint16_t)publen;
	memcpy(ft->ft_data, pubdata, publen);

	return m_entry;
}

void
os_log_coprocessor_register(const char *uuid, const char *file_path, bool copy)
{
	uint64_t                 stamp;
	size_t                   path_size = strlen(file_path) + 1;
	firehose_tracepoint_id_u trace_id;
	size_t                   uuid_info_len = sizeof(struct firehose_trace_uuid_info_s) + path_size;
	union {
		struct firehose_trace_uuid_info_s uuid_info;
		char path[PATH_MAX + sizeof(struct firehose_trace_uuid_info_s)];
	} buf;

	if (os_log_disabled()) {
		return;
	}

	if (path_size > PATH_MAX) {
		return;
	}

	// write metadata to uuid_info
	memcpy(buf.uuid_info.ftui_uuid, uuid, sizeof(uuid_t));
	buf.uuid_info.ftui_size    = 1;
	buf.uuid_info.ftui_address = 1;

	stamp = firehose_tracepoint_time(firehose_activity_flags_default);

	// create tracepoint id
	trace_id.ftid_value = FIREHOSE_TRACE_ID_MAKE(firehose_tracepoint_namespace_metadata, _firehose_tracepoint_type_metadata_coprocessor,
	    (firehose_tracepoint_flags_t)0, copy ? firehose_tracepoint_code_load_memory : firehose_tracepoint_code_load_filesystem);

	// write path to buffer
	memcpy(buf.uuid_info.ftui_path, file_path, path_size);

	// send metadata tracepoint to firehose for coprocessor registration in logd
	firehose_trace_metadata(firehose_stream_metadata, trace_id, stamp, (void *)&buf, uuid_info_len);
	return;
}

#ifdef KERNEL
void
firehose_trace_metadata(firehose_stream_t stream, firehose_tracepoint_id_u ftid,
    uint64_t stamp, const void *pubdata, size_t publen)
{
	oslog_stream_buf_entry_t m_entry = NULL;

	if (os_log_disabled()) {
		return;
	}

	// If streaming mode is not on, only log  the metadata
	// in the persistence buffer

	stream_lock();
	if (!oslog_stream_open) {
		stream_unlock();
		goto finish;
	}
	stream_unlock();

	// Setup and write the stream metadata entry
	m_entry = oslog_stream_create_buf_entry(oslog_stream_link_type_metadata, ftid,
	    stamp, pubdata, publen);
	if (!m_entry) {
		counter_inc(&oslog_s_error_count);
		goto finish;
	}

	stream_lock();
	if (!oslog_stream_open) {
		stream_unlock();
		kheap_free(KHEAP_DEFAULT, m_entry,
		    sizeof(struct oslog_stream_buf_entry_s) +
		    sizeof(struct firehose_tracepoint_s) + publen);
		goto finish;
	}
	oslog_s_metadata_msgcount++;
	oslog_streamwrite_metadata_locked(m_entry);
	stream_unlock();

finish:
	_firehose_trace(stream, ftid, stamp, pubdata, publen, true);
}
#endif

void
__firehose_buffer_push_to_logd(firehose_buffer_t fb __unused, bool for_io __unused)
{
	oslogwakeup();
	return;
}

void
__firehose_allocate(vm_offset_t *addr, vm_size_t size __unused)
{
	firehose_chunk_t kernel_buffer = (firehose_chunk_t)kernel_firehose_addr;

	if (kernel_firehose_addr) {
		*addr = kernel_firehose_addr;
	} else {
		*addr = 0;
		return;
	}
	// Now that we are done adding logs to this chunk, set the number of writers to 0
	// Without this, logd won't flush when the page is full
	firehose_boot_chunk->fc_pos.fcp_refcnt = 0;
	memcpy(&kernel_buffer[FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT - 1], (const void *)firehose_boot_chunk, FIREHOSE_CHUNK_SIZE);
	return;
}
// There isnt a lock held in this case.
void
__firehose_critical_region_enter(void)
{
	disable_preemption();
	return;
}

void
__firehose_critical_region_leave(void)
{
	enable_preemption();
	return;
}

#ifdef CONFIG_XNUPOST

#include <tests/xnupost.h>
#define TESTOSLOGFMT(fn_name) "%u^%llu/%llu^kernel^0^test^" fn_name
#define TESTOSLOGPFX "TESTLOG:%u#"
#define TESTOSLOG(fn_name) TESTOSLOGPFX TESTOSLOGFMT(fn_name "#")

extern u_int32_t RandomULong(void);
extern size_t find_pattern_in_buffer(const char *pattern, size_t len, size_t expected_count);
void test_oslog_default_helper(uint32_t uniqid, uint64_t count);
void test_oslog_info_helper(uint32_t uniqid, uint64_t count);
void test_oslog_debug_helper(uint32_t uniqid, uint64_t count);
void test_oslog_error_helper(uint32_t uniqid, uint64_t count);
void test_oslog_fault_helper(uint32_t uniqid, uint64_t count);
void _test_log_loop(void * arg __unused, wait_result_t wres __unused);
void test_oslog_handleOSLogCtl(int32_t * in, int32_t * out, int32_t len);
kern_return_t test_stresslog_dropmsg(uint32_t uniqid);

kern_return_t test_os_log(void);
kern_return_t test_os_log_parallel(void);

#define GENOSLOGHELPER(fname, ident, callout_f)                                                            \
    void fname(uint32_t uniqid, uint64_t count)                                                            \
    {                                                                                                      \
	int32_t datalen = 0;                                                                               \
	uint32_t checksum = 0;                                                                             \
	char databuffer[256];                                                                              \
	T_LOG("Doing os_log of %llu TESTLOG msgs for fn " ident, count);                                   \
	for (uint64_t i = 0; i < count; i++)                                                               \
	{                                                                                                  \
	    datalen = scnprintf(databuffer, sizeof(databuffer), TESTOSLOGFMT(ident), uniqid, i + 1, count); \
	    checksum = crc32(0, databuffer, datalen);                                                      \
	    callout_f(OS_LOG_DEFAULT, TESTOSLOG(ident), checksum, uniqid, i + 1, count);                   \
	/*T_LOG(TESTOSLOG(ident), checksum, uniqid, i + 1, count);*/                                   \
	}                                                                                                  \
    }

GENOSLOGHELPER(test_oslog_info_helper, "oslog_info_helper", os_log_info);
GENOSLOGHELPER(test_oslog_fault_helper, "oslog_fault_helper", os_log_fault);
GENOSLOGHELPER(test_oslog_debug_helper, "oslog_debug_helper", os_log_debug);
GENOSLOGHELPER(test_oslog_error_helper, "oslog_error_helper", os_log_error);
GENOSLOGHELPER(test_oslog_default_helper, "oslog_default_helper", os_log);

kern_return_t
test_os_log()
{
	char databuffer[256];
	uint32_t uniqid = RandomULong();
	size_t match_count = 0;
	uint32_t checksum = 0;
	uint32_t total_msg = 0;
	uint32_t saved_msg = 0;
	uint32_t dropped_msg = 0;
	size_t datalen = 0;
	uint64_t a = mach_absolute_time();
	uint64_t seqno = 1;
	uint64_t total_seqno = 2;

	os_log_t log_handle = os_log_create("com.apple.xnu.test.t1", "kpost");

	T_ASSERT_EQ_PTR(&_os_log_default, log_handle, "os_log_create returns valid value.");
	T_ASSERT_EQ_INT(TRUE, os_log_info_enabled(log_handle), "os_log_info is enabled");
	T_ASSERT_EQ_INT(TRUE, os_log_debug_enabled(log_handle), "os_log_debug is enabled");
	T_ASSERT_EQ_PTR(&_os_log_default, OS_LOG_DEFAULT, "ensure OS_LOG_DEFAULT is _os_log_default");

	total_msg = counter_load(&oslog_p_total_msgcount);
	saved_msg = counter_load(&oslog_p_saved_msgcount);
	dropped_msg = counter_load(&oslog_p_dropped_msgcount);
	T_LOG("oslog internal counters total %u , saved %u, dropped %u", total_msg, saved_msg, dropped_msg);

	T_LOG("Validating with uniqid %u u64 %llu", uniqid, a);
	T_ASSERT_NE_UINT(0, uniqid, "random number should not be zero");
	T_ASSERT_NE_ULLONG(0, a, "absolute time should not be zero");

	datalen = scnprintf(databuffer, sizeof(databuffer), TESTOSLOGFMT("printf_only"), uniqid, seqno, total_seqno);
	checksum = crc32(0, databuffer, datalen);
	printf(TESTOSLOG("printf_only") "mat%llu\n", checksum, uniqid, seqno, total_seqno, a);

	seqno += 1;
	datalen = scnprintf(databuffer, sizeof(databuffer), TESTOSLOGFMT("printf_only"), uniqid, seqno, total_seqno);
	checksum = crc32(0, databuffer, datalen);
	printf(TESTOSLOG("printf_only") "mat%llu\n", checksum, uniqid, seqno, total_seqno, a);

	datalen = scnprintf(databuffer, sizeof(databuffer), "kernel^0^test^printf_only#mat%llu", a);
	match_count = find_pattern_in_buffer(databuffer, datalen, total_seqno);
	T_EXPECT_EQ_ULONG(match_count, total_seqno, "verify printf_only goes to systemlog buffer");

	uint32_t logging_config = atm_get_diagnostic_config();
	T_LOG("checking atm_diagnostic_config 0x%X", logging_config);

	if ((logging_config & ATM_TRACE_OFF) || (logging_config & ATM_TRACE_DISABLE)) {
		T_LOG("ATM_TRACE_OFF / ATM_TRACE_DISABLE is set. Would not see oslog messages. skipping the rest of test.");
		return KERN_SUCCESS;
	}

	/* for enabled logging printfs should be saved in oslog as well */
	T_EXPECT_GE_UINT((counter_load(&oslog_p_total_msgcount) - total_msg), 2, "atleast 2 msgs should be seen by oslog system");

	a = mach_absolute_time();
	total_seqno = 1;
	seqno = 1;
	total_msg = counter_load(&oslog_p_total_msgcount);
	saved_msg = counter_load(&oslog_p_saved_msgcount);
	dropped_msg = counter_load(&oslog_p_dropped_msgcount);
	datalen = scnprintf(databuffer, sizeof(databuffer), TESTOSLOGFMT("oslog_info"), uniqid, seqno, total_seqno);
	checksum = crc32(0, databuffer, datalen);
	os_log_info(log_handle, TESTOSLOG("oslog_info") "mat%llu", checksum, uniqid, seqno, total_seqno, a);
	T_EXPECT_GE_UINT((counter_load(&oslog_p_total_msgcount) - total_msg), 1, "total message count in buffer");

	datalen = scnprintf(databuffer, sizeof(databuffer), "kernel^0^test^oslog_info#mat%llu", a);
	match_count = find_pattern_in_buffer(databuffer, datalen, total_seqno);
	T_EXPECT_EQ_ULONG(match_count, total_seqno, "verify oslog_info does not go to systemlog buffer");

	total_msg = counter_load(&oslog_p_total_msgcount);
	test_oslog_info_helper(uniqid, 10);
	T_EXPECT_GE_UINT(counter_load(&oslog_p_total_msgcount) - total_msg, 10, "test_oslog_info_helper: Should have seen 10 msgs");

	total_msg = counter_load(&oslog_p_total_msgcount);
	test_oslog_debug_helper(uniqid, 10);
	T_EXPECT_GE_UINT(counter_load(&oslog_p_total_msgcount) - total_msg, 10, "test_oslog_debug_helper:Should have seen 10 msgs");

	total_msg = counter_load(&oslog_p_total_msgcount);
	test_oslog_error_helper(uniqid, 10);
	T_EXPECT_GE_UINT(counter_load(&oslog_p_total_msgcount) - total_msg, 10, "test_oslog_error_helper:Should have seen 10 msgs");

	total_msg = counter_load(&oslog_p_total_msgcount);
	test_oslog_default_helper(uniqid, 10);
	T_EXPECT_GE_UINT(counter_load(&oslog_p_total_msgcount) - total_msg, 10, "test_oslog_default_helper:Should have seen 10 msgs");

	total_msg = counter_load(&oslog_p_total_msgcount);
	test_oslog_fault_helper(uniqid, 10);
	T_EXPECT_GE_UINT(counter_load(&oslog_p_total_msgcount) - total_msg, 10, "test_oslog_fault_helper:Should have seen 10 msgs");

	T_LOG("oslog internal counters total %u , saved %u, dropped %u", counter_load(&oslog_p_total_msgcount), counter_load(&oslog_p_saved_msgcount),
	    counter_load(&oslog_p_dropped_msgcount));

	return KERN_SUCCESS;
}

static uint32_t _test_log_loop_count = 0;
void
_test_log_loop(void * arg __unused, wait_result_t wres __unused)
{
	uint32_t uniqid = RandomULong();
	test_oslog_debug_helper(uniqid, 100);
	os_atomic_add(&_test_log_loop_count, 100, relaxed);
}

kern_return_t
test_os_log_parallel(void)
{
	thread_t thread[2];
	kern_return_t kr;
	uint32_t uniqid = RandomULong();

	printf("oslog internal counters total %lld , saved %lld, dropped %lld", counter_load(&oslog_p_total_msgcount), counter_load(&oslog_p_saved_msgcount),
	    counter_load(&oslog_p_dropped_msgcount));

	kr = kernel_thread_start(_test_log_loop, NULL, &thread[0]);
	T_ASSERT_EQ_INT(kr, KERN_SUCCESS, "kernel_thread_start returned successfully");

	kr = kernel_thread_start(_test_log_loop, NULL, &thread[1]);
	T_ASSERT_EQ_INT(kr, KERN_SUCCESS, "kernel_thread_start returned successfully");

	test_oslog_info_helper(uniqid, 100);

	/* wait until other thread has also finished */
	while (_test_log_loop_count < 200) {
		delay(1000);
	}

	thread_deallocate(thread[0]);
	thread_deallocate(thread[1]);

	T_LOG("oslog internal counters total %lld , saved %lld, dropped %lld", counter_load(&oslog_p_total_msgcount), counter_load(&oslog_p_saved_msgcount),
	    counter_load(&oslog_p_dropped_msgcount));
	T_PASS("parallel_logging tests is now complete");

	return KERN_SUCCESS;
}

void
test_oslog_handleOSLogCtl(int32_t * in, int32_t * out, int32_t len)
{
	if (!in || !out || len != 4) {
		return;
	}
	switch (in[0]) {
	case 1:
	{
		/* send out counters */
		out[1] = counter_load(&oslog_p_total_msgcount);
		out[2] = counter_load(&oslog_p_saved_msgcount);
		out[3] = counter_load(&oslog_p_dropped_msgcount);
		out[0] = KERN_SUCCESS;
		break;
	}
	case 2:
	{
		/* mini stress run */
		out[0] = test_os_log_parallel();
		break;
	}
	case 3:
	{
		/* drop msg tests */
		out[1] = RandomULong();
		out[0] = test_stresslog_dropmsg(out[1]);
		break;
	}
	case 4:
	{
		/* invoke log helpers */
		uint32_t uniqid = in[3];
		int32_t msgcount = in[2];
		if (uniqid == 0 || msgcount == 0) {
			out[0] = KERN_INVALID_VALUE;
			return;
		}

		switch (in[1]) {
		case OS_LOG_TYPE_INFO: test_oslog_info_helper(uniqid, msgcount); break;
		case OS_LOG_TYPE_DEBUG: test_oslog_debug_helper(uniqid, msgcount); break;
		case OS_LOG_TYPE_ERROR: test_oslog_error_helper(uniqid, msgcount); break;
		case OS_LOG_TYPE_FAULT: test_oslog_fault_helper(uniqid, msgcount); break;
		case OS_LOG_TYPE_DEFAULT:
		default: test_oslog_default_helper(uniqid, msgcount); break;
		}
		out[0] = KERN_SUCCESS;
		break;
		/* end of case 4 */
	}
	default:
	{
		out[0] = KERN_INVALID_VALUE;
		break;
	}
	}
	return;
}

kern_return_t
test_stresslog_dropmsg(uint32_t uniqid)
{
	uint32_t total, saved, dropped;
	total = counter_load(&oslog_p_total_msgcount);
	saved = counter_load(&oslog_p_saved_msgcount);
	dropped = counter_load(&oslog_p_dropped_msgcount);
	uniqid = RandomULong();
	test_oslog_debug_helper(uniqid, 100);
	while ((counter_load(&oslog_p_dropped_msgcount) - dropped) == 0) {
		test_oslog_debug_helper(uniqid, 100);
	}
	printf("test_stresslog_dropmsg: logged %lld msgs, saved %lld and caused a drop of %lld msgs. \n", counter_load(&oslog_p_total_msgcount) - total,
	    counter_load(&oslog_p_saved_msgcount) - saved, counter_load(&oslog_p_dropped_msgcount) - dropped);
	return KERN_SUCCESS;
}

#endif

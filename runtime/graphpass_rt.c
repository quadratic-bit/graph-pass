#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

#ifdef __linux__
#include <sys/syscall.h>
#include <unistd.h>
#endif

static FILE *glog_file = NULL;
static atomic_uint_fast64_t glog_seq = 1;

static uint64_t glog_thread_id(void) {
#ifdef __linux__
	return (uint64_t)syscall(SYS_gettid);
#else
	return 0;
#endif
}

static FILE *glog_open_file(void) {
	if (glog_file) return glog_file;

	const char *path = getenv("GRAPH_PASS_LOG");
	if (!path || !*path) path = "graphpass.glog";

	glog_file = fopen(path, "a");
	if (!glog_file) return NULL;

	setvbuf(glog_file, NULL, _IOLBF, 0);
	return glog_file;
}

static void glog_write_event(const char *tag, uint64_t id) {
	FILE *f = glog_open_file();
	if (!f) return;

	uint64_t seq = atomic_fetch_add_explicit(&glog_seq, 1, memory_order_relaxed);
	uint64_t tid = glog_thread_id();

	flockfile(f);
	fprintf(f, "%" PRIu64 "\t%" PRIu64 "\t%s\t%" PRIu64 "\n", seq, tid, tag, id);
	funlockfile(f);
}

void __graphpass_log_init(uint64_t module_id) {
	FILE *f = glog_open_file();
	if (!f) return;

	flockfile(f);
	fprintf(f, "GLOG\t1\t%" PRIu64 "\n", module_id);
	funlockfile(f);
}

void __graphpass_log_bb(uint64_t bblock_id) {
	glog_write_event("BB", bblock_id);
}

void __graphpass_log_edge(uint64_t edge_id) {
	glog_write_event("EDGE", edge_id);
}

void __graphpass_log_call(uint64_t inst_id) {
	glog_write_event("CALL", inst_id);
}

__attribute__((destructor))
static void __graphpass_log_fini(void) {
	if (!glog_file) return;
	fclose(glog_file);
	glog_file = NULL;
}

#undef TRACE_SYSTEM
#define TRACE_SYSTEM interconnect

#if !defined(_TRACE_INTERCONNECT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_INTERCONNECT_H

#include <linux/tracepoint.h>

struct interconnect_path;
struct interconnect_creq;

DECLARE_EVENT_CLASS(interconnect_path,

	TP_PROTO(struct interconnect_path *path,
		struct interconnect_creq *creq),

	TP_ARGS(path, creq),

	TP_STRUCT__entry(
		__field(struct interconnect_path *, path)
		__field(size_t, num_nodes)
		__field(u32, avg_bw)
		__field(u32, peak_bw)
	),

	TP_fast_assign(
		__entry->path = path;
		__entry->num_nodes = path->num_nodes;
		__entry->avg_bw = creq->avg_bw;
		__entry->peak_bw = creq->avg_bw;
	),

	TP_printk("INTERCONNECT: %p num_nodes=%zu avg_bw=%u peak_bw=%u",
		__entry->path,
		__entry->num_nodes,
		__entry->avg_bw,
		__entry->peak_bw)
);

DEFINE_EVENT(interconnect_path, interconnect_set,

	TP_PROTO(struct interconnect_path *path, struct interconnect_creq *creq),

	TP_ARGS(path, creq)
);

DEFINE_EVENT(interconnect_path, interconnect_set_complete,

	TP_PROTO(struct interconnect_path *path, struct interconnect_creq *creq),

	TP_ARGS(path, creq)
);

#endif /* _TRACE_INTERCONNECT_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

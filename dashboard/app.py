"""Streamlit dashboard for the cache & memory hierarchy simulator.

Run from the project root:
    streamlit run dashboard/app.py
"""
from __future__ import annotations

import sys
from dataclasses import replace
from pathlib import Path

import pandas as pd
import plotly.express as px
import streamlit as st

sys.path.insert(0, str(Path(__file__).resolve().parent))
import simulator as sim  # noqa: E402

st.set_page_config(page_title="Cache Simulator", layout="wide")

# Fixed categorical order (validated palette): colour follows the entity, so
# LRU is always blue, FIFO orange, Random aqua, on every chart.
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]
POLICY_COLORS = {"LRU": SERIES[0], "FIFO": SERIES[1], "Random": SERIES[2]}
PATTERN_COLORS = {"sequential": SERIES[0], "random": SERIES[1], "strided": SERIES[2], "file": SERIES[3]}
WRITE_COLORS = {"write-back": SERIES[0], "write-through": SERIES[1]}
C3_COLORS = {"compulsory": SERIES[0], "capacity": SERIES[1], "conflict": SERIES[2]}
SIZES = ["1K", "2K", "4K", "8K", "16K", "32K", "64K", "128K", "256K", "512K", "1M", "2M", "4M", "8M"]


def style(fig, y_pct: bool = False):
    fig.update_layout(margin=dict(l=10, r=10, t=80, b=10), legend_title_text="",
                      title=dict(y=0.97, yanchor="top"),
                      legend=dict(orientation="h", y=1.02, yanchor="bottom", x=0), bargap=0.25, bargroupgap=0.08)
    fig.update_traces(marker_line_width=0, selector=dict(type="bar"))
    fig.update_traces(line_width=2, marker_size=8, selector=dict(type="scatter"))
    if y_pct:
        fig.update_yaxes(tickformat=".0%", rangemode="tozero")
    fig.update_xaxes(showgrid=False)
    return fig


@st.cache_data(show_spinner=False)
def run_cached(specs: tuple[sim.RunSpec, ...], binary: str) -> list[dict]:
    return sim.run_many(list(specs), Path(binary))


@st.cache_data(show_spinner=False)
def sample_cached(trace: sim.Trace, binary: str, n: int):
    return sim.sample_addresses(trace, Path(binary), n)


# --------------------------------------------------------------------------- binary
binary = sim.find_binary()
if binary is None:
    st.title("Cache & Memory Hierarchy Simulator")
    st.warning("The `cachesim` binary was not found. Build it with CMake (or set `CACHESIM_BIN`).")
    if st.button("Build now (cmake)"):
        with st.spinner("Building C++ simulator..."):
            ok, log = sim.build_binary()
        st.code(log[-4000:])
        if ok:
            st.rerun()
    st.stop()

# --------------------------------------------------------------------------- sidebar
with st.sidebar:
    st.header("Workload")
    pattern = st.selectbox("Access pattern", ["random", "sequential", "strided", "file"])
    trace_path = ""
    if pattern == "file":
        bundled = sorted(str(p) for p in (sim.PROJECT_ROOT / "traces").glob("*.trace"))
        trace_path = st.selectbox("Trace file", bundled) if bundled else st.text_input("Trace path")
    accesses = st.select_slider("Accesses", [10_000, 50_000, 100_000, 200_000, 500_000, 1_000_000], 200_000)
    working_set = st.select_slider("Working set", SIZES, "1M")
    element = st.selectbox("Element size (bytes)", [1, 2, 4, 8, 16], index=2)
    stride = st.select_slider("Stride (strided pattern)", ["8", "16", "32", "64", "128", "256", "512", "1K", "2K", "4K", "8K"], "4K")
    write_ratio = st.slider("Write ratio", 0.0, 1.0, 0.3, 0.05)
    seed = st.number_input("Seed", 0, 2**31 - 1, 42)
    mem_latency = st.number_input("Main-memory latency (cycles)", 1, 2000, 200)
    st.caption(f"Simulator: `{binary}`")

trace = sim.Trace(pattern=pattern, accesses=accesses, working_set=working_set, element=element,
                  stride=stride, write_ratio=write_ratio, seed=int(seed), path=trace_path)

st.title("Cache & Memory Hierarchy Simulator")
st.caption("Every number below is produced by the C++ `cachesim` binary; this page only drives it and plots the JSON.")

tab_run, tab_policy, tab_sweep, tab_pattern = st.tabs(
    ["Hierarchy", "Policy comparison", "Parameter sweep", "Access patterns"])

# --------------------------------------------------------------------------- hierarchy tab
DEFAULTS = [sim.Level("L1", "32K", "8", 64, "lru", "wb", 4),
            sim.Level("L2", "256K", "8", 64, "lru", "wb", 12),
            sim.Level("L3", "2M", "16", 64, "lru", "wb", 40)]


def level_editor(i: int, d: sim.Level) -> sim.Level:
    st.markdown(f"**{d.name}**")
    size = st.select_slider("Size", SIZES, d.size, key=f"size{i}")
    org = st.selectbox("Organisation", ["direct-mapped", "2-way", "4-way", "8-way", "16-way", "fully-assoc"],
                       index={"1": 0, "2": 1, "4": 2, "8": 3, "16": 4}.get(d.assoc, 3), key=f"org{i}")
    assoc = {"direct-mapped": "1", "fully-assoc": "full"}.get(org, org.split("-")[0])
    block = st.selectbox("Block (bytes)", [16, 32, 64, 128, 256], index=2, key=f"blk{i}")
    policy = st.selectbox("Replacement", ["lru", "fifo", "random"], key=f"pol{i}", format_func=str.upper)
    write = st.selectbox("Write policy", ["wb", "wt"], key=f"wp{i}",
                         format_func=lambda w: "write-back + allocate" if w == "wb" else "write-through + no-allocate")
    hit = st.number_input("Hit latency (cycles)", 1, 500, d.hit, key=f"hit{i}")
    return sim.Level(d.name, size, assoc, block, policy, write, int(hit))


with tab_run:
    n_levels = st.radio("Cache levels", [1, 2, 3], index=2, horizontal=True)
    cols = st.columns(n_levels)
    levels = []
    for i in range(n_levels):
        with cols[i]:
            levels.append(level_editor(i, DEFAULTS[i]))
    spec = sim.RunSpec(tuple(levels), trace, int(mem_latency))
    try:
        (res,) = run_cached((spec,), str(binary))
    except sim.SimulationError as e:
        st.error(str(e))
        st.stop()

    m = st.columns(4)
    m[0].metric("AMAT (measured)", f"{res['amat']:.2f} cycles")
    m[1].metric("AMAT (formula)", f"{res['amat_formula']:.2f} cycles",
                help="h1 + m1·(h2 + m2·(… + Tmem)) with local miss rates. Lower than measured never; "
                     "higher when write-through stores are absorbed by the write buffer.")
    m[2].metric("L1 hit rate", f"{res['levels'][0]['hit_rate']:.1%}")
    m[3].metric("Memory traffic", f"{(res['memory']['bytes_read'] + res['memory']['bytes_written']) / 1024:,.0f} KB")

    df = pd.DataFrame(res["levels"])
    c1, c2 = st.columns(2)
    with c1:
        fig = px.bar(df, x="name", y="hit_rate", text_auto=".1%", title="Local hit rate per level",
                     color_discrete_sequence=[SERIES[0]], labels={"name": "", "hit_rate": "Hit rate"})
        st.plotly_chart(style(fig, True), use_container_width=True)
    with c2:
        c3 = df.melt(id_vars="name", value_vars=["compulsory", "capacity", "conflict"], var_name="kind", value_name="count")
        fig = px.bar(c3, x="name", y="count", color="kind", title="Miss breakdown (3C model)",
                     color_discrete_map=C3_COLORS, labels={"name": "", "count": "Misses"})
        st.plotly_chart(style(fig), use_container_width=True)

    show = df[["name", "size_bytes", "organisation", "sets", "block_bytes", "replacement", "write_policy",
               "hit_latency", "accesses", "hits", "misses", "hit_rate", "global_miss_rate", "compulsory",
               "capacity", "conflict", "evictions", "writebacks", "write_throughs"]]
    st.dataframe(show.style.format({"hit_rate": "{:.2%}", "global_miss_rate": "{:.2%}"}), use_container_width=True,
                 hide_index=True)
    st.caption(f"Main memory: {res['memory']['reads']:,} reads, {res['memory']['writes']:,} writes · "
               f"{res['trace']['unique_blocks']:,} unique blocks touched")
    with st.expander("Equivalent command line"):
        st.code(" \\\n  ".join(sim.command(spec, binary, "text")), language="bash")
    st.download_button("Download JSON", data=pd.Series(res).to_json(), file_name="cachesim_result.json")

# --------------------------------------------------------------------------- policy tab
with tab_policy:
    st.subheader("Replacement and write policies across access patterns")
    st.caption("A working set about twice the L1 size is where replacement policies actually have to choose. "
               "AMAT counts cycles the CPU waits: write-through stores drain through a write buffer and never stall, "
               "while write-back store misses fetch the block first, so write-through can show the lower AMAT and "
               "pays in write traffic instead.")
    pc = st.columns(5)
    l1_size = pc[0].select_slider("L1 size", SIZES, "32K", key="p_size")
    l1_assoc = pc[1].selectbox("L1 ways", ["1", "2", "4", "8", "16"], index=2, key="p_assoc")
    p_ws = pc[2].select_slider("Working set", SIZES, "64K", key="p_ws")
    with_l2 = pc[3].checkbox("Add 256K L2", value=True)
    patterns = pc[4].multiselect("Patterns", ["sequential", "random", "strided"], ["sequential", "random", "strided"])

    def hierarchy(policy: str, write: str) -> tuple[sim.Level, ...]:
        l1 = sim.Level("L1", l1_size, l1_assoc, 64, policy, write, 4)
        return (l1, sim.Level("L2", "256K", "8", 64, policy, "wb", 12)) if with_l2 else (l1,)

    grid = [(p, pol, wp) for p in patterns for pol in ["lru", "fifo", "random"] for wp in ["wb", "wt"]]
    specs = tuple(sim.RunSpec(hierarchy(pol, wp), replace(trace, pattern=p, working_set=p_ws), int(mem_latency))
                  for p, pol, wp in grid)
    if specs:
        with st.spinner(f"Running {len(specs)} simulations..."):
            results = run_cached(specs, str(binary))
        rows = []
        for (p, pol, wp), r in zip(grid, results):
            l1 = r["levels"][0]
            rows.append({"pattern": p, "policy": l1["replacement"], "write policy": l1["write_policy"],
                         "L1 hit rate": l1["hit_rate"], "AMAT": r["amat"],
                         "memory KB written": r["memory"]["bytes_written"] / 1024,
                         "L1 write traffic KB": (l1["writebacks"] * l1["block_bytes"]
                                                 + l1["write_throughs"] * trace.element) / 1024,
                         "memory KB read": r["memory"]["bytes_read"] / 1024})
        pdf = pd.DataFrame(rows)
        wb = pdf[pdf["write policy"] == "write-back"]
        c1, c2 = st.columns(2)
        with c1:
            fig = px.bar(wb, x="pattern", y="L1 hit rate", color="policy", barmode="group", text_auto=".1%",
                         title="L1 hit rate by replacement policy", color_discrete_map=POLICY_COLORS)
            st.plotly_chart(style(fig, True), use_container_width=True)
        with c2:
            fig = px.bar(wb, x="pattern", y="AMAT", color="policy", barmode="group", text_auto=".1f",
                         title="AMAT (cycles) by replacement policy", color_discrete_map=POLICY_COLORS)
            st.plotly_chart(style(fig), use_container_width=True)
        lru = pdf[pdf["policy"] == "LRU"]
        c3, c4 = st.columns(2)
        with c3:
            fig = px.bar(lru, x="pattern", y="L1 write traffic KB", color="write policy", barmode="group",
                         text_auto=",.0f", title="Write traffic leaving L1 (KB, LRU)", color_discrete_map=WRITE_COLORS)
            st.plotly_chart(style(fig), use_container_width=True)
        with c4:
            fig = px.bar(lru, x="pattern", y="AMAT", color="write policy", barmode="group", text_auto=".1f",
                         title="AMAT (cycles) by write policy, LRU", color_discrete_map=WRITE_COLORS)
            st.plotly_chart(style(fig), use_container_width=True)
        st.dataframe(pdf.style.format({"L1 hit rate": "{:.2%}", "AMAT": "{:.2f}", "memory KB written": "{:,.0f}",
                                       "L1 write traffic KB": "{:,.0f}", "memory KB read": "{:,.0f}"}), use_container_width=True, hide_index=True)
        st.download_button("Download CSV", pdf.to_csv(index=False), "policy_comparison.csv")

# --------------------------------------------------------------------------- sweep tab
with tab_sweep:
    st.subheader("Sweep one L1 parameter")
    sc = st.columns(3)
    param = sc[0].selectbox("Parameter", ["L1 size", "Associativity", "Block size"])
    sweep_policy = sc[1].selectbox("Replacement", ["lru", "fifo", "random"], format_func=str.upper, key="s_pol")
    sweep_patterns = sc[2].multiselect("Patterns", ["sequential", "random", "strided"], ["sequential", "random", "strided"],
                                       key="s_pat")
    if param == "L1 size":
        values = st.multiselect("Sizes", SIZES, ["4K", "8K", "16K", "32K", "64K", "128K", "256K", "512K", "1M"])
        mk = lambda v: sim.Level("L1", v, "8", 64, sweep_policy, "wb", 4)  # noqa: E731
    elif param == "Associativity":
        values = st.multiselect("Ways", ["1", "2", "4", "8", "16", "full"], ["1", "2", "4", "8", "16", "full"])
        mk = lambda v: sim.Level("L1", "32K", v, 64, sweep_policy, "wb", 4)  # noqa: E731
    else:
        values = st.multiselect("Block sizes", ["16", "32", "64", "128", "256"], ["16", "32", "64", "128", "256"])
        mk = lambda v: sim.Level("L1", "32K", "8", int(v), sweep_policy, "wb", 4)  # noqa: E731
    sweep_l2 = st.checkbox("Keep a fixed 1M 16-way L2 (12 cycles) below L1", value=True)
    grid = [(p, v) for p in sweep_patterns for v in values]
    specs = tuple(sim.RunSpec((mk(v),) + ((sim.Level("L2", "1M", "16", 256, "lru", "wb", 12),) if sweep_l2 else ()),
                              replace(trace, pattern=p), int(mem_latency)) for p, v in grid)
    if specs:
        with st.spinner(f"Running {len(specs)} simulations..."):
            results = run_cached(specs, str(binary))
        sdf = pd.DataFrame([{"pattern": p, param: v, "order": values.index(v), "L1 hit rate": r["levels"][0]["hit_rate"],
                             "AMAT": r["amat"], "conflict": r["levels"][0]["conflict"],
                             "capacity": r["levels"][0]["capacity"], "compulsory": r["levels"][0]["compulsory"]}
                            for (p, v), r in zip(grid, results)]).sort_values(["pattern", "order"])
        c1, c2 = st.columns(2)
        with c1:
            fig = px.line(sdf, x=param, y="L1 hit rate", color="pattern", markers=True, title=f"L1 hit rate vs {param}",
                          color_discrete_map=PATTERN_COLORS)
            fig.update_xaxes(type="category")
            st.plotly_chart(style(fig, True), use_container_width=True)
        with c2:
            fig = px.line(sdf, x=param, y="AMAT", color="pattern", markers=True, title=f"AMAT (cycles) vs {param}",
                          color_discrete_map=PATTERN_COLORS)
            fig.update_xaxes(type="category")
            st.plotly_chart(style(fig), use_container_width=True)
        st.dataframe(sdf.drop(columns="order").style.format({"L1 hit rate": "{:.2%}", "AMAT": "{:.2f}"}),
                     use_container_width=True, hide_index=True)
        st.download_button("Download CSV", sdf.drop(columns="order").to_csv(index=False), "sweep.csv")

# --------------------------------------------------------------------------- patterns tab
with tab_pattern:
    st.subheader("What the generated address streams look like")
    n = st.slider("Accesses to plot", 200, 5000, 1500, 100)
    pats = ["sequential", "random", "strided"] if pattern != "file" else ["file"]
    cols = st.columns(len(pats))
    for col, p in zip(cols, pats):
        rows = sample_cached(replace(trace, pattern=p), str(binary), n)
        if not rows:
            continue
        base = min(a for a, _ in rows)
        adf = pd.DataFrame({"access #": range(len(rows)), "offset (bytes)": [a - base for a, _ in rows],
                            "op": ["write" if o == "W" else "read" for _, o in rows]})
        fig = px.scatter(adf, x="access #", y="offset (bytes)", color="op", title=p,
                         color_discrete_map={"read": SERIES[0], "write": SERIES[1]})
        fig.update_traces(marker_size=4)
        col.plotly_chart(style(fig), use_container_width=True)
    st.markdown(
        "- **Sequential** walks the array element by element: one compulsory miss per block, then hits "
        "(spatial locality), so hit rate ≈ 1 − element/block.\n"
        "- **Random** picks elements uniformly in the working set: hit rate ≈ cache size / working set once warm.\n"
        "- **Strided** jumps `stride` bytes each access (column-major walk). A stride that is a multiple of "
        "`sets × block` maps every access to the same set, causing conflict misses even when the cache is large.")

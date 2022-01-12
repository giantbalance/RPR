#!/usr/bin/env python3
import os
import sys
import struct
import matplotlib.pyplot as plt
import matplotlib
from tqdm import tqdm

TYPE_SHIFT = 60
TYPE_REF = 0x0
TYPE_MALLOC = 0x8
TYPE_CALLOC = 0x9
TYPE_REALLOC = 0xa
TYPE_FREE = 0xb
TYPE_ICOUNT = 0xf
ADDR_MASK = (0x1 << TYPE_SHIFT) - 1

PAGE_SHIFT = 12
PAGE_SIZE = 0x1 << PAGE_SHIFT
PAGE_MASK = ~(PAGE_SIZE - 1)

MEM_AREA_THRESHOLD = 10 * PAGE_SIZE


def PAGE_ALIGN(addr):
    return (addr + PAGE_SIZE - 1) & PAGE_MASK


def ENTRY_TYPE(addr):
    return addr >> TYPE_SHIFT


def ENTRY_ADDR(addr):
    return addr & ADDR_MASK


def addr_to_vpn(addr):
    return addr >> PAGE_SHIFT


def parse_args(argv):
    argc = len(argv)
    if argc != 2:
        print("usage: %s <trace file>" % argv[0])
        sys.exit(1)

    trace_file = argv[1]
    trace_name = os.path.splitext(os.path.basename(trace_file))[0]

    return trace_name, trace_file


def extract(trace_file, object_only):
    print("Extracting traces..")

    objects = []
    local_traces = [[]]
    global_trace = []

    def find_object(addr):
        for idx, obj in enumerate(objects):
            obj_start = obj['start']
            obj_end = obj['end']
            obsolete = obj['obsolete']
            if obsolete:
                continue
            if obj_start <= addr and addr < obj_end:
                return idx + 1
        return 0

    def reg_ref(addr, size):
        oidx = find_object(addr)
        vpn = addr_to_vpn(addr)
        if not local_traces[oidx] or local_traces[oidx][-1] != vpn:
            local_traces[oidx].append(vpn)
        if not global_trace or global_trace[-1] != vpn:
            global_trace.append(vpn)

    def alloc_object(addr, size):
        req_start = addr
        req_end = addr + size
        start = PAGE_ALIGN(req_start)
        end = PAGE_ALIGN(req_end)
        for obj in objects:
            obj_start = obj['start']
            obj_end = obj['end']
            obsolete = obj['obsolete']
            if obsolete:
                continue
            if obj_start < end and start < obj_end:
                print("overlapping memory object")
                sys.exit(1)

        if end - start < MEM_AREA_THRESHOLD:
            return

        objects.append({'start': start, 'end': end,
            'req_start': req_start, 'req_end': req_end, 'obsolete': False})
        local_traces.append([])

    def free_object(addr):
        for obj in objects:
            if obj['req_start'] == addr:
                obj['obsolete'] = True

    def malloc_object(addr, size):
        alloc_object(addr, size)

    def calloc_object(addr, nmemb, size):
        alloc_object(addr, nmemb * size)

    def realloc_object(addr, ptr, size):
        free_object(ptr)
        alloc_object(addr, size)

    with open(trace_file, 'rb') as f:
        trace_data = f.read()

    pos_prev = 0
    pos = 0
    end_pos = len(trace_data)
    pbar = tqdm(total = end_pos)
    while pos < end_pos:
        addr = struct.unpack("L", trace_data[pos:pos+8])[0]
        pos = pos + 8
        trace_type = ENTRY_TYPE(addr)
        addr = ENTRY_ADDR(addr)

        if trace_type == TYPE_REF:
            ref_size = struct.unpack("i", trace_data[pos:pos+4])[0]
            pos = pos + 4
            reg_ref(addr, ref_size)

        elif trace_type == TYPE_MALLOC:
            r_size = struct.unpack("L", trace_data[pos:pos+8])[0]
            pos = pos + 8
            malloc_object(addr, r_size)

        elif trace_type == TYPE_CALLOC:
            m_nmemb = struct.unpack("L", trace_data[pos:pos+8])[0]
            pos = pos + 8
            m_size = struct.unpack("L", trace_data[pos:pos+8])[0]
            pos = pos + 8
            calloc_object(addr, m_nmemb, m_size)

        elif trace_type == TYPE_REALLOC:
            m_ptr = struct.unpack("L", trace_data[pos:pos+8])[0]
            pos = pos + 8
            m_size = struct.unpack("L", trace_data[pos:pos+8])[0]
            pos = pos + 8
            realloc_object(addr, m_ptr, m_size)

        elif trace_type == TYPE_FREE:
            free_object(addr)

        elif trace_type != TYPE_ICOUNT:
            # wrong path
            print("Wrong trace format..\n")
            sys.exit(1)

        if pos - pos_prev >= end_pos // 1000:
            pbar.update(pos - pos_prev)
            pos_prev = pos

    pbar.update(pos - pos_prev)

    return global_trace, local_traces


def visualize(global_trace, local_traces, name):
    print("Drawing plots..")

    os.makedirs(name, exist_ok = True)

    matplotlib.rcParams.update({'font.size': 28})
    pbar = tqdm(total = len(local_traces) + 1)
    fig, ax = plt.subplots(figsize = (8, 7))
    ax.plot(global_trace, 'k.', markersize = 0.3)
    fig.subplots_adjust(bottom = 0.18, top = 0.9, left = 0.1, right = 0.9)
    plt.ticklabel_format(useMathText = True)
    plt.yticks([])
    plt.xlabel("Virtual time", labelpad = 20)
    plt.ylabel("Memory address space", labelpad = 15)
    plt.savefig(os.path.join(name, "global.png"), dpi = 600)
    pbar.update(1)

    for idx, local_trace in enumerate(local_traces):
        plt.cla()
        fig, ax = plt.subplots(figsize = (8, 7))
        ax.plot(local_trace, 'k.', markersize = 0.3)
        fig.subplots_adjust(bottom = 0.18, top = 0.9, left = 0.1, right = 0.9)
        plt.ticklabel_format(useMathText = True)
        plt.yticks([])
        plt.xlabel("Virtual time", labelpad = 20)
        plt.ylabel("Memory address space", labelpad = 15)
        plt.savefig(os.path.join(name, f"{idx}.png"), dpi = 600)
        pbar.update(1)


def main():
    object_only = True
    trace_name, trace_file = parse_args(sys.argv)
    global_trace, local_traces = extract(trace_file, object_only)
    visualize(global_trace, local_traces, trace_name)


if __name__ == "__main__":
    main()

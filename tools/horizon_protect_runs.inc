// FNV-nx: coalesce only identical mapping transitions within one view/chunk.
// Never round a watch outwards or change pages outside the requested range.
for (size_t p = begin; p < end;) {
    const bool was = (AccessOf(s, p) == REX_GM_WRITE);
    size_t stop = p + kPageSize;
    if (was != must_be) {
        if (const View* v = ViewFor(s, wbase + p)) {
            const size_t off_mapping = v->offset + (p - static_cast<size_t>(v->base - s.base));
            size_t vi = 0;
            Chunk* c = ChunkAt(s, off_mapping);
            const bool in_view = c && ViewIndexFor(s, wbase + p, &vi) &&
                                 s.view_mapped.count({vi, c->offset}) != 0;
            if (in_view) {
                const size_t limit = std::min({end,
                    static_cast<size_t>(v->base - s.base) + v->length,
                    p + c->length - (off_mapping - c->offset)});
                while (stop < limit && (AccessOf(s, stop) == REX_GM_WRITE) == was)
                    stop += kPageSize;
                const size_t run = stop - p;
                const u64 src = reinterpret_cast<u64>(static_cast<uint8_t*>(c->shadow) +
                                                       (off_mapping - c->offset));
                ++s.fnv_protect_runs;
                s.fnv_protect_pages += run / kPageSize;
                if (!ProtectWithPermissions(s.base + p, was, run)) {
                    const Result rc = was ? svcUnmapProcessMemory(s.base + p, Proc(), src, run)
                                          : svcMapProcessMemory(s.base + p, Proc(), src, run);
                    if (R_FAILED(rc)) {
                        s.last_result = rc;
                        ++s.fnv_protect_failures;
                        // Throttled diagnostics, using already locked state (no recursive getters).
                        if (s.fnv_protect_failures <= 8 || !(s.fnv_protect_failures % 256)) {
                            std::fprintf(stderr, "FNV protect failure rc=%08X addr=%llX run=%zu "
                                "watch_pages=%zu chunks=%zu views=%zu mapped=%zu "
                                "runs=%llu pages=%llu failures=%llu\n", unsigned(rc),
                                static_cast<unsigned long long>(wbase + p), run,
                                s.protection.size(), s.chunks.size(), s.view_mapped.size(), s.mapped,
                                static_cast<unsigned long long>(s.fnv_protect_runs),
                                static_cast<unsigned long long>(s.fnv_protect_pages),
                                static_cast<unsigned long long>(s.fnv_protect_failures));
                            std::fflush(stderr);
                        }
                        return false;
                    }
                    if (was) s.mapped -= run;
                    else s.mapped += run;
                }
            }
        }
    }
    // Record logical permissions only after the complete host operation succeeds.
    for (size_t page = p; page < stop; page += kPageSize) {
        if (must_be) s.protection.erase(page);
        else s.protection[page] = access;
    }
    p = stop;
}

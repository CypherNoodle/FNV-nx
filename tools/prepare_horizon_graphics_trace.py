"""Bounded graphics metadata, deferred pass/barrier counters and sparse boundaries."""
import argparse
from pathlib import Path
import re
BEGIN='// FNV graphics trace begin'
END='// FNV graphics trace end'
def remove_trace(text):
    return re.sub(r'^([ \t]*)'+BEGIN+r'\n.*?^\1'+END+r'\n','',text,flags=re.M|re.S)

def prepare(sdk):
    names=['src/graphics/vulkan/command_processor.cpp',
           'include/rex/graphics/vulkan/deferred_command_buffer.h',
           'src/graphics/vulkan/deferred_command_buffer.cpp',
           'src/graphics/vulkan/render_target_cache.cpp']
    originals={n:remove_trace((sdk/n).read_text()) for n in names}
    texts=dict(originals)
    def add(n,anchor,code,before=False,indent=''):
        assert texts[n].count(anchor)==1,(n,anchor)
        block=indent+BEGIN+'\n'+indent+'#if REX_PLATFORM_SWITCH\n'+code.rstrip()+'\n'+indent+'#endif\n'+indent+END+'\n'
        texts[n]=texts[n].replace(anchor,block+anchor if before else anchor+block,1)
    cp,h,cpp,rt=names
    for n in names:
        anchor=re.search(r'^#include[^\n]+\n',texts[n],re.M)[0]
        add(n,anchor,'#include <rex/graphics/fnv_graphics_trace.h>')
    add(h,'    assert_null(render_pass_begin->pNext);\n',
        '    RexFnvGraphicsBegin(false, render_pass_begin);',indent='    ')
    add(cpp,'void DeferredCommandBuffer::CmdVkBeginRendering(const VkRenderingInfo* rendering_info) {\n',
        '  RexFnvGraphicsBegin(true, rendering_info);',indent='  ')
    add(cpp,'    const VkImageMemoryBarrier* image_memory_barriers) {\n',
        '  RexFnvGraphicsBarrier(src_stage_mask, dst_stage_mask, memory_barrier_count,\n'
        '                        buffer_memory_barrier_count, image_memory_barrier_count);',indent='  ')
    add(cp,'  const VulkanPipelineCache::PipelineLayoutProvider* pipeline_layout_provider;\n',
        '''  if (RexFnvGraphicsTraceEnabled()) {
    const uint32_t fnv_colors[4]={
        regs.Get<reg::RB_COLOR_INFO>(XE_GPU_REG_RB_COLOR_INFO).value,
        regs.Get<reg::RB_COLOR_INFO>(XE_GPU_REG_RB_COLOR1_INFO).value,
        regs.Get<reg::RB_COLOR_INFO>(XE_GPU_REG_RB_COLOR2_INFO).value,
        regs.Get<reg::RB_COLOR_INFO>(XE_GPU_REG_RB_COLOR3_INFO).value};
    RexFnvGraphicsState(render_target_cache_->GetPath()==RenderTargetCache::Path::kPixelShaderInterlock,
                        regs.Get<reg::RB_SURFACE_INFO>().value, regs.Get<reg::RB_DEPTH_INFO>().value,
                        normalized_depth_control.value, normalized_color_mask, fnv_colors);
  }''',before=True,indent='  ')
    add(rt,'  VkImage image;\n  VkDeviceMemory memory;\n',
        '  RexFnvGraphicsImage(&image_create_info, key.is_depth, uint32_t(key.resource_format), key.base_tiles);',
        before=True,indent='  ')
    add(rt,'  RexSwitchPerfCount(9);\n',
        '''  if (RexFnvGraphicsTraceEnabled() && resolve_info.copy_dest_extent_length) {
    uint32_t fnv_base,fnv_used,fnv_rows,fnv_pitch;
    resolve_info.GetCopyEdramTileSpan(fnv_base,fnv_used,fnv_rows,fnv_pitch);
    RexFnvGraphicsResolve(GetPath()==Path::kPixelShaderInterlock,resolve_info.copy_dest_base,
                          resolve_info.copy_dest_extent_length,fnv_base,fnv_used,fnv_rows,fnv_pitch);
  }''',indent='  ')
    # Checkpoint every existing guest pass boundary without replacing any command.
    pattern=r'(?m)^([ \t]*)(deferred_command_buffer_\.CmdVk(BeginRendering|BeginRenderPass|EndRendering|EndRenderPass)\()'
    def checkpoint(m):
        indent=m[1]
        kind=1 if m[3].startswith('Begin') else 2
        return (indent+BEGIN+'\n'+indent+'#if REX_PLATFORM_SWITCH\n'+
                indent+f'fnv_gpu_probe_.Boundary(deferred_command_buffer_, {kind});\n'+
                indent+'#endif\n'+indent+END+'\n'+m[0])
    texts[cp],count=re.subn(pattern,checkpoint,texts[cp])
    assert count==12,('Pinned SDK pass callsites changed',count)
    for n,t in texts.items():
        assert remove_trace(t)==originals[n],n
        if (sdk/n).read_bytes()!=t.encode(): (sdk/n).write_bytes(t.encode())
    source=Path(__file__).resolve().parents[1]/'src/switch_graphics_trace.h'
    destination=sdk/'include/rex/graphics/fnv_graphics_trace.h'
    if not destination.exists() or destination.read_bytes()!=source.read_bytes(): destination.write_bytes(source.read_bytes())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,required=True)
    prepare(p.parse_args().sdk)
    print('PASS graphics trace: original commands preserved; 12 pass boundaries; bounded metadata')

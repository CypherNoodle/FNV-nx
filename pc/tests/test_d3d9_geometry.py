"""COM bindings/buffer oracle with retained real vertex/index data; no draws."""
import argparse
from pathlib import Path
import struct
from test_d3d9_com import test,SLOTS

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output','shaders','vertices','indices'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);a=p.parse_args()
    vb=a.vertices.read_bytes();ib=a.indices.read_bytes()
    if not vb or len(vb)%20 or not ib or len(ib)%2 or max(struct.unpack('<'+'H'*(len(ib)//2),ib))>=len(vb)//20:
        raise ValueError('Need bounded captured vertices (stride20) and index16 data')
    slots=SLOTS+[(93,2),(107,2),(108,2),(26,7),(27,7),(11,5),(12,1),(13,2),(10,1),
        (100,5),(101,5),(104,2),(105,2),(102,3),(103,3),(82,7),(86,3),(87,2),(88,2),(4,3)]
    modes=('normal','unknown','unsupported','stale','unregistered','parent','capacity','alias','nested','bound_parent','decl_parent')
    test(a,host_source='d3d9_geometry_host.c',slots=slots,modes=modes,strict_fault_cases=6,
        extra_args=(a.vertices.resolve(),a.indices.resolve()),extra_inputs=(Path(__file__).resolve(),))

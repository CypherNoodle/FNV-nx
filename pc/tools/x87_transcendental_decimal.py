"""Independent bounded mathematical oracle, never part of guest execution.

Decimal exp/ln and a half-angle arctangent series provide a reference separate
from both native x87 and target libm. Results are not x87 microcode emulation.
"""
from decimal import Decimal,localcontext
from functools import lru_cache
import struct


def value(data):
    sig,se=struct.unpack('<QH',data);e=se&0x7fff
    if not 0<e<0x7fff or not sig>>63 or abs(e-0x3fff)>200:raise ValueError('Bounded canonical normals only')
    return Decimal(-sig if se&0x8000 else sig)*(Decimal(2)**(e-0x3fff-63))


def atan_series(x):
    square=x*x;term=x;total=x;index=1
    while True:
        term=-term*square;next_value=total+term/(2*index+1)
        if next_value==total:return total
        total=next_value;index+=1
        if index>10000:raise ValueError('Series did not converge')


def atan(x,pi):
    sign=-1 if x<0 else 1;x=abs(x);reciprocal=x>1
    if reciprocal:x=1/x
    for unused in range(4):x=x/(1+(1+x*x).sqrt())
    result=atan_series(x)*16
    return sign*(pi/2-result if reciprocal else result)


def round_normal(x,rc):
    if not x:return bytes(10),0
    sign=0x8000 if x<0 else 0;n,d=abs(x).as_integer_ratio();e=n.bit_length()-d.bit_length()
    below=(n < d<<e) if e>=0 else (n<<(-e) < d)
    if below:e-=1
    shift=63-e
    if shift>=0:q,r=divmod(n<<shift,d);den=d
    else:q,r=divmod(n,d<<(-shift));den=d<<(-shift)
    up=(2*r>den or (2*r==den and q&1)) if rc==0 else bool(r and ((rc==1 and sign) or (rc==2 and not sign)))
    q+=int(up)
    if q==1<<64:q>>=1;e+=1
    if not 0<e+0x3fff<0x7fff:raise ValueError('Normal result required')
    return struct.pack('<QH',q,sign|(e+0x3fff)),int(up)


@lru_cache(maxsize=4096)
def reference(op,x_bytes,y_bytes):
    """Require identical rounded payload/C1 at two independent precisions."""
    answers=[]
    for precision in (210,310):
        with localcontext() as ctx:
            ctx.prec=precision;x=value(x_bytes);y=value(y_bytes)
            if op==0:
                if not -1<x<1 or not x:raise ValueError('Interior nonzero F2XM1 only')
                result=(x*Decimal(2).ln()).exp()-1
            elif op==1:
                pi=16*atan_series(Decimal(1)/5)-4*atan_series(Decimal(1)/239)
                result=atan(y/x,pi)
                if x<0:result+=pi if y>0 else -pi
            elif op==2:
                if x<=0 or x==1 or not y:raise ValueError('Nontrivial FYL2X normals only')
                result=y*x.ln()/Decimal(2).ln()
            else:raise ValueError('Unknown operation')
            answers.append(tuple(round_normal(result,rc) for rc in range(4)))
    if answers[0]!=answers[1]:raise ValueError('Precision-dependent oracle')
    return answers[0]

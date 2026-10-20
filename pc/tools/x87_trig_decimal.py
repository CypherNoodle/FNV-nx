"""Independent true-pi Decimal trig oracle for bounded synthetic inputs."""
from decimal import Decimal,localcontext,ROUND_FLOOR
from functools import lru_cache
from x87_transcendental_decimal import value,round_normal,atan_series


@lru_cache(maxsize=4096)
def reference(payload):
    answers=[]
    for precision in (210,310):
        with localcontext() as ctx:
            ctx.prec=precision;x=value(payload);negative=x<0;x=abs(x)
            pi=16*atan_series(Decimal(1)/5)-4*atan_series(Decimal(1)/239)
            n=int((x/(pi/2)+Decimal('0.5')).to_integral_value(rounding=ROUND_FLOOR));r=x-n*(pi/2)
            a=sa=r;b=cb=Decimal(1);square=r*r;k=1
            while True:
                a=-a*square/(2*k*(2*k+1));b=-b*square/((2*k-1)*2*k)
                na,nb=sa+a,cb+b
                if na==sa and nb==cb:break
                sa,cb=na,nb;k+=1
                if k>10000:raise ValueError('Series did not converge')
            q=n%4
            sine,cosine=((sa,cb),(cb,-sa),(-sa,-cb),(-cb,sa))[q]
            if negative:sine=-sine
            tangent=sine/cosine
            answers.append(tuple(tuple(round_normal(v,rc) for rc in range(4)) for v in (sine,cosine,tangent)))
    if answers[0]!=answers[1]:raise ValueError('Precision-dependent trig reference')
    return answers[0]

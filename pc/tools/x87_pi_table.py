"""Reproducible own 256-bit 2/pi constant; integer Machin series only."""
def two_over_pi_words():
    def calculate(guard):
        unit=1<<(256+guard)
        def atan_inverse(q):
            power=unit//q;total=0;k=0
            while power:
                term=power//(2*k+1);total+=term if not k%2 else -term
                power//=q*q;k+=1
            return total
        pi=16*atan_inverse(5)-4*atan_inverse(239)
        return (2*unit*unit//pi)>>guard
    value=calculate(96)
    if value!=calculate(192):raise ValueError('Unstable 2/pi constant')
    return [(value>>(64*i))&((1<<64)-1) for i in range(4)]

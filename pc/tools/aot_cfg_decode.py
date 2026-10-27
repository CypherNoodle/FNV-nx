"""Opt-in bounded CFG decoding for explicitly selected private function ranges.

Uses the SDK's reachable-edge/table decoder, never linear sweep across embedded
data. Does not infer new function bounds or modify SDK sources. Translation and
differential verification remain separate requirements.
"""
def install(addresses):
    from tools.recomp.translator import FunctionTranslator
    selected=frozenset(addresses)
    if not selected or any(type(x) is not int or not 0<x<0x100000000 for x in selected):
        raise ValueError('Require explicit valid function starts')
    original=FunctionTranslator.decode_function
    if getattr(original,'_fnv_cfg_selected',None) is not None:raise ValueError('CFG overlay already installed')
    def decode(self,start,end):
        if start in selected and start not in self._recovered_cfg:
            if not 0<end-start<=65536:raise ValueError('CFG owner extent exceeds bounded profile')
            recovered=self._recover_cfg(start,end,set(),set())
            if not recovered:raise ValueError('CFG recovery failed')
            instructions,tables,_=recovered
            stop=start
            for instruction in instructions:
                if instruction.address<stop or instruction.end_address>end:raise ValueError('Overlapping/out-of-range CFG decode')
                stop=instruction.end_address
            self._recovered_cfg[start]=dict(end=end,instructions=instructions,jump_tables=tables)
        return original(self,start,end)
    decode._fnv_cfg_selected=selected
    FunctionTranslator.decode_function=decode

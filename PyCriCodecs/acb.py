from .chunk import *
from .utf import UTF, UTFBuilder
from .awb import AWB

class ACB:
    """ An ACB is basically a giant @UTF table. Use this class to extract any ACB. """
    __slots__ = ["filename", "payload"]
    payload: list[dict]

    def __init__(self, filename) -> None:
        self.payload = UTF(filename).get_payload()
        self.acbparse(self.payload)
    
    def acbparse(self, payload) -> None:
        """ Recursively parse the payload. """
        for dict in range(len(payload)):
            for k, v in payload[dict].items():
                if v[0] == UTFTypeValues.bytes:
                    if v[1].startswith(UTFType.UTF.value): #or v[1].startswith(UTFType.EUTF.value): # ACB's never gets encrypted? 
                        par = UTF(v[1]).get_payload()
                        payload[dict][k] = par
                        self.acbparse(par)
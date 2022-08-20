from .chunk import *
from .utf import UTF, UTFBuilder
from .awb import AWB, AWBBuilder
import os

class ACB(UTF):
    """ An ACB is basically a giant @UTF table. Use this class to extract any ACB. """
    __slots__ = ["filename", "payload", "dirname", "filename"]
    payload: list[dict]
    dirname: str
    filename: str

    def __init__(self, filename, dirname: str = "") -> None:
        self.payload = UTF(filename).get_payload()
        self.filename = filename
        self.acbparse(self.payload)
        self.dirname = dirname
        # TODO check on ACB version.
    
    def acbparse(self, payload: list[dict]) -> None:
        """ Recursively parse the payload. """
        for dict in range(len(payload)):
            for k, v in payload[dict].items():
                if v[0] == UTFTypeValues.bytes:
                    if v[1].startswith(UTFType.UTF.value): #or v[1].startswith(UTFType.EUTF.value): # ACB's never gets encrypted? 
                        par = UTF(v[1]).get_payload()
                        payload[dict][k] = par
                        self.acbparse(par)
    
    def extract(self):
        # There are two types of ACB's, one that has an AWB file inside it,
        # and one with an AWB pair.
        if self.payload[0]['AwbFile'][1] == b'':
            if type(self.filename) == str:
                awbObj = AWB(os.path.join(os.path.dirname(self.filename), self.payload[0]['Name'][1]+".awb"))
            else:
                awbObj = AWB(self.payload[0]['Name'][1]+".awb")
        else:
            awbObj = AWB(self.payload[0]['AwbFile'][1])
        
        # Sort indexes based on AWB's IDs if they exist on there, otherwise sort normally. Although seemingly the AWB IDs are always sorted.
        names = sorted(self.payload[0]['CueNameTable'], key=lambda x: awbObj.ids.index(x['CueIndex'][1]) if x['CueIndex'][1] in awbObj.ids else x['CueIndex'][1]) # A bit compute expensive.
        cnt = 0
        if self.dirname != "":
            os.makedirs(self.dirname, exist_ok=True)
        for i in awbObj.getfiles():
            open(os.path.join(self.dirname, names[cnt]["CueName"][1]), "wb").write(i)
            cnt += 1

# Need to finish HCA encoding first. TODO
class ACBBuilder:
    pass
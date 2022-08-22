from struct import iter_unpack
from .chunk import *
from .utf import UTF, UTFBuilder
from .awb import AWB, AWBBuilder
from .hca import HCA
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
    
    def extract(self, decode=False, key=0):
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
        sqnce = self.payload[0]['SequenceTable'] # Perhaps it's not good to sort the names above, since it might conflict with those. Although I never saw them get mangled.
        stream_ids = [x["CueName"][1] for x in names]
        flnames = []
        if len(set(stream_ids)) != len(self.payload[0]['TrackTable']):
            for i in range(len(sqnce)):
                if sqnce[i]['TrackIndex'][1] != b'':
                    for num in iter_unpack(">H", sqnce[i]['TrackIndex'][1]):
                        flnames.append(stream_ids[i]+"_"+str(num[0]))
        else:
            flnames = stream_ids

        cnt = 0
        if self.dirname != "":
            os.makedirs(self.dirname, exist_ok=True)
        for i in awbObj.getfiles():
            if decode:
                open(os.path.join(self.dirname, flnames[cnt]+".wav"), "wb").write(HCA(i, key=key, subkey=awbObj.subkey).decode())
                cnt += 1
            else:
                if self.payload[0]['WaveformTable'][cnt]['EncodeType'][1] == 2: # As far as I saw, this is HCA.
                    open(os.path.join(self.dirname, flnames[cnt]+".hca"), "wb").write(i)
                    cnt += 1
                else:
                    open(os.path.join(self.dirname, flnames[cnt]), "wb").write(i)
                    cnt += 1

# Need to finish HCA encoding first. TODO
class ACBBuilder:
    pass
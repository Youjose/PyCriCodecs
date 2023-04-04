from struct import iter_unpack
from .chunk import *
from .utf import UTF, UTFBuilder
from .awb import AWB, AWBBuilder
from .hca import HCA
import os

# TODO revamp the whole ACB class. ACB is a lot more complex with those @UTF tables.
class ACB(UTF):
    """ An ACB is basically a giant @UTF table. Use this class to extract any ACB. """
    __slots__ = ["filename", "payload", "filename", "awb"]
    payload: list
    filename: str
    awb: AWB

    def __init__(self, filename) -> None:
        self.payload = UTF(filename).get_payload()
        self.filename = filename
        self.acbparse(self.payload)
        # TODO check on ACB version.
    
    def acbparse(self, payload: list) -> None:
        """ Recursively parse the payload. """
        for dict in range(len(payload)):
            for k, v in payload[dict].items():
                if v[0] == UTFTypeValues.bytes:
                    if v[1].startswith(UTFType.UTF.value): #or v[1].startswith(UTFType.EUTF.value): # ACB's never gets encrypted? 
                        par = UTF(v[1]).get_payload()
                        payload[dict][k] = par
                        self.acbparse(par)
        self.load_awb()
    
    def load_awb(self) -> None:
        # There are two types of ACB's, one that has an AWB file inside it,
        # and one with an AWB pair.
        if self.payload[0]['AwbFile'][1] == b'':
            if type(self.filename) == str:
                awbObj = AWB(os.path.join(os.path.dirname(self.filename), self.payload[0]['Name'][1]+".awb"))
            else:
                awbObj = AWB(self.payload[0]['Name'][1]+".awb")
        else:
            awbObj = AWB(self.payload[0]['AwbFile'][1])
        self.awb = awbObj
    
    # revamping...
    def exp_extract(self, decode: bool = False, key = 0):
        # There are two types of ACB's, one that has an AWB file inside it,
        # and one with an AWB pair. Or multiple AWB's.

        # TODO Add multiple AWB loading.
        if self.payload[0]['AwbFile'][1] == b'':
            if type(self.filename) == str:
                awbObj = AWB(os.path.join(os.path.dirname(self.filename), self.payload[0]['Name'][1]+".awb"))
            else:
                awbObj = AWB(self.payload[0]['Name'][1]+".awb")
        else:
            awbObj = AWB(self.payload[0]['AwbFile'][1])

        pl = self.payload[0]
        names = [] # Where all filenames will end up in.
        # cuename > cue > block > sequence > track > track_event > command > synth > waveform
        # seems to be the general way to do it, some may repeat, and some may go back to other tables.
        # I will try to make this code go through all of them in advance. 

        """ Load Cue names and indexes. """
        cue_names_and_indexes: list = []
        for i in pl["CueNameTable"]:
            cue_names_and_indexes.append((i["CueIndex"], i["CueName"]))
        srt_names = sorted(cue_names_and_indexes, key=lambda x: x[0])
        
        """ Go through all cues and match wavforms or names. """
        for i in cue_names_and_indexes:

            cue_Info = pl["CueTable"][i[0]]
            ref_type = cue_Info["ReferenceType"][1]
            wavform = pl["WaveformTable"][i[0]]

            if ref_type == 1:
                usememory: bool = wavform['Streaming'][1] == 0

                if "Id" in wavform:
                    wavform["MemoryAwbId"] = wavform["Id"] # Old ACB's use "Id", so we default it to the new MemoryAwbId slot.

                if usememory:
                    assert len(wavform['MemoryAwbId']) == len(srt_names) # Will error if not so. TODO add extracting without filenames references.
                    names = [y[1][1] for _,y in sorted(zip([x[1] for x in pl["WaveformTable"]], srt_names), key=lambda z: z[0])]
                    break # We break, since we did everything in the line above. I don't think ref_type changes between cues.

                else:
                    # TODO
                    raise NotImplementedError("ACB needs multiple AWB's, not unsupported yet.")

            elif ref_type == 2:
                # TODO
                raise NotImplementedError("Unsupported ReferenceType.")

            elif ref_type == 3:
                sequence = pl['SequenceTable'][i[0]]
                track_type = sequence['Type'][1] # Unused but will leave it here if needed.
                for tr_idx in iter_unpack(">H", sequence['TrackIndex'][1]):
                    # TODO I am here currently.
                    pass

            elif ref_type == 8:
                # TODO
                raise NotImplementedError("Unsupported ReferenceType.")

            else:
                raise NotImplementedError("Unknown ReferenceType inside ACB.")
        
    def parse_type1(self):
        pass

    def parse_type2(self):
        pass

    def parse_type3(self):
        pass

    def parse_type8(self):
        pass

    def parse_cues(self):
        pass

    def parse_synth(self):
        pass

    def parse_wavform(self):
        pass

    def parse_tracktable(self):
        pass

    def parse_commands(self):
        pass

    def parse_sequence(self):
        pass

    def extract(self, decode: bool = False, key: int = 0, dirname: str = ""):
        """ Extracts audio files in an AWB/ACB without preserving filenames. """
        if dirname:
            os.makedirs(dirname, exist_ok=True)
        filename = 0
        for i in self.awb.getfiles():
            Extension: str = self.get_extension(self.payload[0]['WaveformTable'][filename]['EncodeType'][1])
            if decode and Extension == ".hca":
                    hca = HCA(i, key=key, subkey=self.awb.subkey).decode()
                    open(os.path.join(dirname, str(filename)+".wav"), "wb").write(hca)
                    filename += 1
            else:
                open(os.path.join(dirname, f"{filename}{Extension}"), "wb").write(i)
                filename += 1
    
    def get_extension(self, EncodeType: int) -> str:
        if EncodeType == 2:
            return ".hca"
        elif EncodeType == 19:
            return ".mp4"
        else:
            return ""


# TODO Have to finish correct ACB extracting first.
class ACBBuilder(UTFBuilder):
    pass
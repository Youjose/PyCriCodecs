from struct import iter_unpack
from .chunk import *
from .utf import UTF, UTFBuilder
from .awb import AWB, AWBBuilder
from .hca import HCA
import os

# TODO revamp the whole ACB class. ACB is a lot more complex with those @UTF tables.
class ACB(UTF):
    """ An ACB is basically a giant @UTF table. Use this class to extract any ACB. """
    __slots__ = ["filename", "payload", "dirname", "filename"]
    payload: list
    dirname: str
    filename: str

    def __init__(self, filename, dirname: str = "") -> None:
        self.payload = UTF(filename).get_payload()
        self.filename = filename
        self.acbparse(self.payload)
        self.dirname = dirname
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
        cue_names_and_indexes: list[tuple] = []
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
        
        # From here on, it'a all wrong. FIXME.
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
class ACBBuilder(UTFBuilder):
    pass
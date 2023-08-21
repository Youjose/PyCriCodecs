import CriCodecs

class ADX:
    """ADX Modules for decoding and encoding ADX files, pass the either `adx file` or `wav file` in bytes to either `decode` or `encode` respectively."""  
    
    # Decodes ADX to WAV.
    def decode(data: bytes) -> bytes:
        """ Decodes ADX to WAV. """
        return CriCodecs.AdxDecode(bytes(data))
            
    # Encodes WAV to ADX.
    def encode(data: bytes, BitDepth = 0x4, Blocksize = 0x12, Encoding = 3, AdxVersion = 0x4, Highpass_Frequency = 0x1F4, Filter = 0, force_not_looping = False) -> bytes:
        """ Encodes WAV to ADX. """
        return CriCodecs.AdxEncode(bytes(data), BitDepth, Blocksize, Encoding, Highpass_Frequency, Filter, AdxVersion, force_not_looping)
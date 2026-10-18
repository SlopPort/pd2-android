"""Read-only StormLib bindings for local MPQ archives (Windows x64)."""
import ctypes as C
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class MPQ:
    def __init__(self, path):
        self.path = Path(path)
        self.dll = C.WinDLL(str(ROOT / 'vendor/StormLib/x64/StormLib.dll'), use_last_error=True)
        for name, args, ret in [
            ('SFileOpenArchive', [C.c_wchar_p,C.c_uint32,C.c_uint32,C.POINTER(C.c_void_p)], C.c_bool),
            ('SFileOpenFileEx', [C.c_void_p,C.c_char_p,C.c_uint32,C.POINTER(C.c_void_p)], C.c_bool),
            ('SFileGetFileSize', [C.c_void_p,C.POINTER(C.c_uint32)], C.c_uint32),
            ('SFileReadFile', [C.c_void_p,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32),C.c_void_p], C.c_bool),
            ('SFileCloseFile', [C.c_void_p], C.c_bool),
            ('SFileCloseArchive', [C.c_void_p], C.c_bool),
        ]:
            f=getattr(self.dll,name); f.argtypes=args; f.restype=ret
        self.handle=C.c_void_p()
        if not self.dll.SFileOpenArchive(str(self.path.resolve()),0,0x100,self.handle):
            raise OSError(C.get_last_error(), str(path))
    def read(self, name):
        h=C.c_void_p()
        if not self.dll.SFileOpenFileEx(self.handle,name.encode('ascii'),0,C.byref(h)):
            raise FileNotFoundError(name)
        try:
            high=C.c_uint32(); size=self.dll.SFileGetFileSize(h,C.byref(high))
            if high.value or size>32*1024*1024: raise ValueError('Member too large')
            b=C.create_string_buffer(size); got=C.c_uint32()
            if not self.dll.SFileReadFile(h,b,size,C.byref(got),None) or got.value!=size:
                raise OSError(C.get_last_error(),name)
            return b.raw
        finally: self.dll.SFileCloseFile(h)
    def close(self): self.dll.SFileCloseArchive(self.handle)

if __name__=='__main__':
    import sys
    for path in sys.argv[1:]:
        a=MPQ(path)
        try:
            names=a.read('(listfile)').decode('latin1').splitlines()
            print(path, len(names))
            print('\n'.join(n for n in names if n.lower().endswith(('.dc6','.dat')) )[:16000])
        except FileNotFoundError: print(path, 'no internal listfile')
        finally: a.close()


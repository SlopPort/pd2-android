package pl.d2port.data;

import java.io.*;
import java.util.*;
import java.util.zip.*;

/** Bounded classic MPQ reader. Hash/decryption algorithms adapted from StormLib
 * (Ladislav Zezula, MIT; see assets/licenses/StormLib.txt). No game DLL required.
 * Initial support: neutral locale, unencrypted files, raw/zlib/PKWARE sectors and single units.
 */
public final class MpqSource implements GameData.Source, Closeable {
    private static final int[] CRYPT=new int[1280];
    static {
        long seed=0x100001;
        for(int x=0;x<256;x++) for(int j=0;j<5;j++) {
            seed=(seed*125+3)%0x2aaaab; int high=(int)(seed&65535)<<16;
            seed=(seed*125+3)%0x2aaaab; CRYPT[x+j*256]=high|(int)(seed&65535);
        }
    }
    private final RandomAccessFile file;
    private final String identity;
    private int[] hashes,blocks;
    private int sector;
    private long size;
    public MpqSource(String identity,File path)throws IOException {
        this.identity=Objects.requireNonNull(identity);
        file=new RandomAccessFile(path,"r");
        fingerprint=path.getCanonicalPath()+":"+path.length()+":"+path.lastModified();
        try {
            size=file.length(); byte[] h=chunk(0,32);
            if(word(h,0)!=0x1a51504d || word(h,4)!=32 || (word(h,12)&65535)!=0)
                throw new IOException("Only classic MPQ at offset zero is supported");
            int shift=word(h,12)>>>16;
            if(shift>15) throw new IOException("Invalid sector size");
            sector=512<<shift;
            long archive=Integer.toUnsignedLong(word(h,8));
            if(archive<32 || archive>size) throw new IOException("Invalid archive size");
            size=archive;
            int hc=word(h,24),bc=word(h,28);
            if(hc<=0 || hc>1048576 || (hc&(hc-1))!=0 || bc<0 || bc>1048576)
                throw new IOException("Unsupported table size");
            hashes=table(Integer.toUnsignedLong(word(h,16)),hc,"(hash table)");
            blocks=table(Integer.toUnsignedLong(word(h,20)),bc,"(block table)");
        } catch(IOException|RuntimeException e){file.close();throw e;}
    }
    public String id(){return identity;}
    private String fingerprint;
    /** Same archive file (path, size, modification time): lets GameData share decompressed resources across sessions. */
    @Override public String fingerprint(){return fingerprint;}
    static int word(byte[] b,int p){return (b[p]&255)|((b[p+1]&255)<<8)|((b[p+2]&255)<<16)|(b[p+3]<<24);}
    static int hash(String name,int type)throws IOException {
        int a=0x7fed7fed,b=0xeeeeeeee;
        for(char c:name.toCharArray()) {
            if(c>127) throw new IOException("Non-ASCII MPQ names unsupported");
            if(c=='/')c='\\'; if(c>='a'&&c<='z')c-=32;
            a=CRYPT[type*256+c]^(a+b); b=c+a+b+(b<<5)+3;
        }
        return a;
    }
    private int[] table(long offset,int count,String name)throws IOException {
        byte[] raw=chunk(offset,count*16); int[] out=new int[count*4];
        int key=hash(name,3),mix=0xeeeeeeee;
        for(int i=0;i<out.length;i++) {
            mix+=CRYPT[1024+(key&255)]; int value=word(raw,i*4)^(key+mix);out[i]=value;
            key=((~key<<21)+0x11111111)|(key>>>11);mix=value+mix+(mix<<5)+3;
        }
        return out;
    }
    private byte[] chunk(long offset,int length)throws IOException {
        if(length<0 || offset<0 || offset>size-length)throw new IOException("MPQ range outside archive");
        byte[] b=new byte[length];file.seek(offset);file.readFully(b);return b;
    }
    public synchronized byte[] read(String path,int limit)throws IOException {
        if(limit<0)throw new IllegalArgumentException("Negative limit");
        path=GameData.canonical(path);
        int n=hashes.length/4,start=hash(path,0)&(n-1),a=hash(path,1),b=hash(path,2),index=-1;
        boolean localized=false;
        for(int i=0;i<n;i++) {
            int p=((start+i)&(n-1))*4,block=hashes[p+3];
            if(block==-1)break;
            if(block==-2 || hashes[p]!=a || hashes[p+1]!=b)continue;
            if(hashes[p+2]!=0){localized=true;continue;}
            index=block;break;
        }
        if(index==-1){if(localized)throw new IOException("Only localized MPQ entry available");return null;}
        if(index<0 || index>=blocks.length/4)throw new IOException("Invalid MPQ block index");
        int p=index*4,packed=blocks[p+1],length=blocks[p+2],flags=blocks[p+3];
        if((flags&0x80000000)==0)throw new IOException("Invalid live MPQ block");
        if((flags&~0x81000300)!=0)throw new IOException("Unsupported MPQ flags: "+Integer.toHexString(flags));
        if(length<0 || length>limit || packed<0 || (long)packed> (long)limit+1048576)
            throw new IOException("MPQ member exceeds limit");
        long offset=Integer.toUnsignedLong(blocks[p]);
        byte[] raw=chunk(offset,packed);
        if((flags&0x300)==0){if(packed!=length)throw new IOException("Raw size mismatch");return raw;}
        if((flags&0x1000000)!=0)return expand(raw,length,(flags&0x100)!=0);
        if(length==0){if(packed!=0)throw new IOException("Invalid empty member");return new byte[0];}
        int count=(int)(((long)length+sector-1)/sector),tableBytes=(count+1)*4;
        if(raw.length<tableBytes || word(raw,0)!=tableBytes)throw new IOException("Invalid sector table");
        byte[] result=new byte[length];
        for(int i=0;i<count;i++) {
            int from=word(raw,i*4),to=word(raw,(i+1)*4),wanted=Math.min(sector,length-i*sector);
            if(from<tableBytes || to<from || to>raw.length)throw new IOException("Invalid sector offset");
            byte[] decoded=expand(Arrays.copyOfRange(raw,from,to),wanted,(flags&0x100)!=0);
            System.arraycopy(decoded,0,result,i*sector,wanted);
        }
        if(word(raw,count*4)!=raw.length)throw new IOException("Trailing sector bytes");
        return result;
    }
    private static byte[] expand(byte[] raw,int wanted,boolean implode)throws IOException {
        if(raw.length==wanted)return raw;
        if(implode)return Pkware.expand(raw,wanted);
        if(raw.length>0 && raw[0]==8)return Pkware.expand(Arrays.copyOfRange(raw,1,raw.length),wanted);
        if(raw.length==0 || raw.length>wanted || raw[0]!=2)throw new IOException("Unsupported MPQ compression (requires zlib)");
        Inflater inflater=new Inflater();
        try {
            inflater.setInput(raw,1,raw.length-1);byte[] out=new byte[wanted];int total=0;
            while(total<wanted && !inflater.finished()) {
                int got=inflater.inflate(out,total,wanted-total);
                if(got==0)throw new IOException("Incomplete zlib stream");total+=got;
            }
            if(total!=wanted || !inflater.finished() || inflater.getRemaining()!=0)throw new IOException("Zlib size/trailer mismatch");
            return out;
        }catch(DataFormatException e){throw new IOException("Invalid zlib data",e);}finally{inflater.end();}
    }
    public synchronized void close()throws IOException{file.close();}
}

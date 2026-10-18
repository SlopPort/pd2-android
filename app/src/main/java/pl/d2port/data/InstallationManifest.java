package pl.d2port.data;

import java.io.*;
import java.security.*;
import java.util.*;

/** Explicit highest-priority-first archive list. No inferred original-game load order. */
public final class InstallationManifest {
    private static final int MAGIC=0x44324931,MAX_ARCHIVES=64;
    public static final class Archive {
        public final String name,sha256;
        public final long size;
        public Archive(String name,String sha256,long size) {
            if(name==null || !name.matches("[A-Za-z0-9][A-Za-z0-9._-]{0,127}") || name.equals(".") || name.equals("..") || sha256==null || !sha256.matches("[0-9a-f]{64}") || size<32 || size>2L*1024*1024*1024)
                throw new IllegalArgumentException("Invalid archive identity");
            this.name=name;this.sha256=sha256;this.size=size;
        }
        public String sourceId(){return name+"@"+sha256;}
    }
    public final String rulesRevision,id;
    public final List<Archive> archives;
    public InstallationManifest(String rulesRevision,List<Archive> archives) {
        if(rulesRevision==null || !rulesRevision.matches("[a-z0-9.-]{1,80}") || archives==null || archives.isEmpty() || archives.size()>MAX_ARCHIVES)
            throw new IllegalArgumentException("Invalid installation manifest");
        Set<String> seen=new HashSet<>();
        for(Archive a:archives)if(a==null || !seen.add(a.name.toLowerCase(Locale.ROOT)))throw new IllegalArgumentException("Duplicate archive name");
        this.rulesRevision=rulesRevision;this.archives=Collections.unmodifiableList(new ArrayList<>(archives));
        id=hex(digest().digest(encode()));
    }
    private static MessageDigest digest(){try{return MessageDigest.getInstance("SHA-256");}catch(NoSuchAlgorithmException e){throw new IllegalStateException(e);}}
    private static String hex(byte[] bytes){StringBuilder result=new StringBuilder();for(byte b:bytes)result.append(String.format(Locale.ROOT,"%02x",b&255));return result.toString();}
    public byte[] encode() {
        try {
            ByteArrayOutputStream bytes=new ByteArrayOutputStream();DataOutputStream out=new DataOutputStream(bytes);
            out.writeInt(MAGIC);out.writeUTF(rulesRevision);out.writeInt(archives.size());
            for(Archive a:archives){out.writeUTF(a.name);out.writeUTF(a.sha256);out.writeLong(a.size);}
            out.flush();return bytes.toByteArray();
        }catch(IOException e){throw new IllegalStateException(e);}
    }
    public static InstallationManifest decode(byte[] bytes)throws IOException {
        if(bytes.length>32768)throw new IOException("Manifest exceeds limit");
        try(DataInputStream input=new DataInputStream(new ByteArrayInputStream(bytes))) {
            if(input.readInt()!=MAGIC)throw new IOException("Unknown manifest version");
            String revision=input.readUTF();int count=input.readInt();
            if(count<1 || count>MAX_ARCHIVES)throw new IOException("Invalid archive count");
            List<Archive> archives=new ArrayList<>();
            for(int i=0;i<count;i++)archives.add(new Archive(input.readUTF(),input.readUTF(),input.readLong()));
            if(input.read()!=-1)throw new IOException("Trailing manifest data");
            return new InstallationManifest(revision,archives);
        }catch(IllegalArgumentException e){throw new IOException("Invalid manifest fields",e);}
    }
    public static Archive inspect(File file)throws IOException {
        long size=file.length();if(size<32 || size>2L*1024*1024*1024)throw new IOException("Archive size outside supported range");
        MessageDigest hash=digest();long read=0;
        try(InputStream in=new FileInputStream(file)) {
            byte[] buffer=new byte[65536];int n;
            while((n=in.read(buffer))!=-1){read+=n;if(read>size)throw new IOException("Archive changed while hashing");hash.update(buffer,0,n);}
        }
        if(read!=size)throw new IOException("Archive changed while hashing");
        return new Archive(file.getName(),hex(hash.digest()),size);
    }
    public interface Locator { File locate(Archive archive)throws IOException; }
    public Session open(File directory)throws IOException {
        File root=directory.getCanonicalFile();
        return open(a->{File file=new File(root,a.name).getCanonicalFile();if(!root.equals(file.getParentFile()))throw new IOException("Archive escapes installation");return file;});
    }
    public Session open(Locator locator)throws IOException {
        List<MpqSource> sources=new ArrayList<>();
        try {
            for(Archive a:archives) {
                File file=locator.locate(a);
                Archive actual=inspect(file);
                if(actual.size!=a.size || !actual.sha256.equals(a.sha256))throw new IOException("Archive checksum mismatch: "+a.name);
                sources.add(new MpqSource(a.sourceId(),file));
            }
            return new Session(sources);
        }catch(IOException|RuntimeException e){for(MpqSource s:sources)try{s.close();}catch(IOException close){e.addSuppressed(close);}throw e;}
    }
    /** Caller must keep archive files immutable for the lifetime of an open session. */
    public static final class Session implements Closeable {
        public final GameData data;
        private final List<MpqSource> sources;
        private Session(List<MpqSource> sources){this.sources=sources;data=new GameData(new ArrayList<GameData.Source>(sources));}
        @Override public void close()throws IOException {
            IOException error=null;for(MpqSource source:sources)try{source.close();}catch(IOException e){if(error==null)error=e;else error.addSuppressed(e);}
            if(error!=null)throw error;
        }
    }
}

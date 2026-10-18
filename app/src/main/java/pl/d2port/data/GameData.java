package pl.d2port.data;

import java.io.*;
import java.util.*;

/** Native resource namespace. Mount order is explicit policy, not inferred D2 load order. */
public final class GameData {
    public interface Source {
        String id();
        // Null means absent only. Corruption/unsupported encoding must throw.
        byte[] read(String canonicalPath, int limit) throws IOException;
        /** Stable identity of the content (null: never cached). */
        default String fingerprint(){return null;}
    }
    public static final class Resource {
        public final String source, path;
        public final byte[] bytes;
        private Resource(String source, String path, byte[] bytes) {
            this.source=source; this.path=path; this.bytes=bytes;
        }
    }
    private final List<Source> sources;
    /** Highest priority first. Sources remain owned by caller. */
    public GameData(List<Source> priorityOrder) {
        Set<String> ids=new HashSet<>();
        for(Source s:priorityOrder)
            if(s==null || s.id()==null || s.id().isEmpty() || !ids.add(s.id()))
                throw new IllegalArgumentException("Missing/duplicate source identity");
        sources=Collections.unmodifiableList(new ArrayList<>(priorityOrder));
    }
    public static String canonical(String path) throws IOException {
        if(path==null || path.isEmpty()) throw new IOException("Empty resource path");
        String n=path.replace('\\','/').toLowerCase(Locale.ROOT);
        for(String part:n.split("/",-1)) {
            if(part.isEmpty() || part.equals(".") || part.equals("..")) throw new IOException("Invalid resource path");
            for(int i=0;i<part.length();i++)
                if(part.charAt(i)<32 || part.charAt(i)==127 || part.charAt(i)==':') throw new IOException("Invalid resource character");
        }
        return n;
    }
    /** Decompressed resources shared by all GameData over the same mounts (level streaming reads the same DT1/DCC/
     * tables again and again). Bytes are treated as read-only by every parser. LRU, bounded in bytes. */
    private static final int CACHE_BYTES=32*1024*1024,CACHE_ENTRY=4*1024*1024;
    private static final LinkedHashMap<String,Resource> CACHE=new LinkedHashMap<>(256,.75f,true);
    private static final Set<String> MISSING=Collections.synchronizedSet(new HashSet<>());
    private static long cached;
    private String mounts;
    private String mounts(){if(mounts==null){StringBuilder b=new StringBuilder();for(Source s:sources){String f=s.fingerprint();if(f==null){mounts="";return mounts;}b.append(s.id()).append('=').append(f).append('|');}mounts=b.toString();}return mounts;}
    public Resource read(String path,int limit) throws IOException {
        if(limit<0) throw new IllegalArgumentException("Negative limit");
        String key=canonical(path),m=mounts(),ck=m+key;boolean cacheable=!m.isEmpty();
        if(cacheable){synchronized(CACHE){Resource r=CACHE.get(ck);if(r!=null&&r.bytes.length<=limit)return r;}
            if(MISSING.contains(ck))throw new FileNotFoundException(key);}
        for(Source source:sources) {
            byte[] bytes=source.read(key,limit);
            if(bytes!=null) {
                if(bytes.length>limit) throw new IOException("Source exceeded resource limit: "+source.id());
                Resource r=new Resource(source.id(),key,bytes);
                if(cacheable&&bytes.length<=CACHE_ENTRY)synchronized(CACHE){
                    if(CACHE.put(ck,r)==null)cached+=bytes.length;
                    Iterator<Map.Entry<String,Resource>> it=CACHE.entrySet().iterator();
                    while(cached>CACHE_BYTES&&it.hasNext()){cached-=it.next().getValue().bytes.length;it.remove();}
                }
                return r;
            }
        }
        if(cacheable&&MISSING.size()<20000)MISSING.add(ck);
        throw new FileNotFoundException(key);
    }
    /** Loose mod files. Resolves only requested path components; no recursive indexing. */
    public static final class Directory implements Source {
        private final String identity;
        private final File root;
        public Directory(String identity,File root) throws IOException {
            this.identity=Objects.requireNonNull(identity); this.root=root.getCanonicalFile();
            if(!this.root.isDirectory()) throw new IOException("Missing data directory");
        }
        public String id(){return identity;}
        public byte[] read(String path,int limit) throws IOException {
            if(limit<0) throw new IllegalArgumentException("Negative limit");
            File current=root;
            for(String component:canonical(path).split("/")) {
                String[] entries=current.list();
                if(entries==null) throw new IOException("Cannot enumerate resource directory");
                File match=null;
                for(String entry:entries) if(entry.toLowerCase(Locale.ROOT).equals(component)) {
                    if(match!=null) throw new IOException("Ambiguous resource case: "+path);
                    match=new File(current,entry);
                }
                if(match==null) return null;
                current=match.getCanonicalFile();
                if(!current.toPath().startsWith(root.toPath())) throw new IOException("Resource escapes data directory");
            }
            if(!current.isFile()) throw new IOException("Resource is not a file");
            if(current.length()>limit) throw new IOException("Resource exceeds limit");
            try(InputStream in=new FileInputStream(current); ByteArrayOutputStream out=new ByteArrayOutputStream()) {
                byte[] buffer=new byte[8192]; int n;
                while((n=in.read(buffer))!=-1) {
                    if(n>limit-out.size()) throw new IOException("Resource exceeds limit");
                    out.write(buffer,0,n);
                }
                return out.toByteArray();
            }
        }
    }
}

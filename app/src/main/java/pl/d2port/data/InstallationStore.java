package pl.d2port.data;
import java.io.*;
import java.nio.file.*;
import java.util.*;
/** Immutable content-addressed archives, atomic active manifest. Caller owns input streams. */
public final class InstallationStore {
 private static final Object WRITE_LOCK=new Object();
 private final File root;
 public InstallationStore(File directory)throws IOException {
  root=directory.getCanonicalFile();if(!root.isDirectory()&&!root.mkdirs())throw new IOException("Cannot create installation store");
 }
 public InstallationManifest current()throws IOException {
  File file=new File(root,"current.manifest");if(!file.exists())return null;
  if(file.length()>32768)throw new IOException("Oversized manifest");
  return InstallationManifest.decode(Files.readAllBytes(file.toPath()));
 }
 private File blob(InstallationManifest.Archive a)throws IOException {
  File file=new File(new File(new File(root,"archives"),a.sha256),a.name).getCanonicalFile();
  if(!file.toPath().startsWith(root.toPath()))throw new IOException("Archive escapes store");return file;
 }
 public InstallationManifest.Session open()throws IOException {
  InstallationManifest current=current();if(current==null)throw new IOException("No installation selected");return current.open(this::blob);
 }
 /** New/replaced basename takes highest priority; all other entries retain order. */
 public InstallationManifest add(String name,InputStream input)throws IOException {
  synchronized(WRITE_LOCK) {
   // Validate before consuming data. Actual digest replaces this placeholder.
   new InstallationManifest.Archive(name,String.join("",Collections.nCopies(64,"0")),32);
   File staging=File.createTempFile("archive-",".tmp",root);
   try {
    try(FileOutputStream out=new FileOutputStream(staging)) {
     byte[] buffer=new byte[65536];long total=0;int n;
     while((n=input.read(buffer))!=-1){total+=n;if(total>2L*1024*1024*1024)throw new IOException("Archive exceeds 2 GiB");out.write(buffer,0,n);}out.getFD().sync();
    }
    InstallationManifest.Archive measured=InstallationManifest.inspect(staging);
    InstallationManifest.Archive entry=new InstallationManifest.Archive(name,measured.sha256,measured.size);
    try(MpqSource check=new MpqSource(entry.sourceId(),staging)){}
    InstallationManifest previous=current();List<InstallationManifest.Archive> entries=new ArrayList<>();entries.add(entry);
    if(previous!=null)for(InstallationManifest.Archive old:previous.archives)if(!old.name.equalsIgnoreCase(name))entries.add(old);
    InstallationManifest next=new InstallationManifest("transitional-rules-v1",entries);
    File target=blob(entry);if(!target.getParentFile().isDirectory()&&!target.getParentFile().mkdirs())throw new IOException("Cannot create archive directory");
    if(target.exists()) {
     InstallationManifest.Archive existing=InstallationManifest.inspect(target);
     if(existing.size!=entry.size||!existing.sha256.equals(entry.sha256))throw new IOException("Stored archive corrupt");
    } else Files.move(staging.toPath(),target.toPath(),StandardCopyOption.ATOMIC_MOVE);
    commit(next);return next;
   }finally{Files.deleteIfExists(staging.toPath());}
  }
 }
 /** Reorder exactly the current archive names. Does not remove content or change files. */
 public InstallationManifest reorder(List<String> names)throws IOException {
  synchronized(WRITE_LOCK) {
   InstallationManifest previous=current();if(previous==null||names.size()!=previous.archives.size())throw new IOException("Invalid reorder");
   Map<String,InstallationManifest.Archive> remaining=new HashMap<>();for(InstallationManifest.Archive a:previous.archives)remaining.put(a.name,a);
   List<InstallationManifest.Archive> ordered=new ArrayList<>();for(String name:names){InstallationManifest.Archive a=remaining.remove(name);if(a==null)throw new IOException("Invalid reorder name");ordered.add(a);}
   InstallationManifest next=new InstallationManifest(previous.rulesRevision,ordered);commit(next);return next;
  }
 }
 private void commit(InstallationManifest manifest)throws IOException {
  byte[] encoded=manifest.encode();File generations=new File(root,"manifests");if(!generations.isDirectory()&&!generations.mkdirs())throw new IOException("Cannot create manifest directory");
  atomicWrite(new File(generations,manifest.id+".manifest"),encoded);
  atomicWrite(new File(root,"current.manifest"),encoded);
 }
 private void atomicWrite(File target,byte[] bytes)throws IOException {
  File temp=File.createTempFile("manifest-",".tmp",root);
  try{try(FileOutputStream out=new FileOutputStream(temp)){out.write(bytes);out.getFD().sync();}Files.move(temp.toPath(),target.toPath(),StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);}
  finally{Files.deleteIfExists(temp.toPath());}
 }
}

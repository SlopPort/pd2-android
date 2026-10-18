package pl.d2port.data;
import java.io.IOException;
/** Altered Java adaptation of blast 1.3 by Mark Adler (2003,2012,2013).
 * Not the original software. Full notice: assets/licenses/blast.h.
 * Bounded output replaces the original sliding-window callback API. */
final class Pkware {
private static final int[] litlen={11,124,8,7,28,7,188,13,76,4,10,8,12,10,12,10,8,23,8,9,7,6,7,8,7,6,55,8,23,24,12,11,7,9,11,12,6,7,22,5,7,24,6,11,9,6,7,22,7,11,38,7,9,8,25,11,8,11,9,12,8,12,5,38,5,38,5,11,7,5,6,21,6,10,53,8,7,24,10,27,44,253,253,253,252,252,252,13,12,45,12,45,12,61,12,45,44,173};
private static final int[] lenlen={2,35,36,53,38,23};
private static final int[] distlen={2,20,53,230,247,151,248};
private static final int[] base={3,2,4,5,6,7,8,9,10,12,16,24,40,72,136,264};
private static final int[] extra={0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8};
private static final class Tree {
 final int[] count=new int[14],symbols;
 Tree(int[] compact) {
  int total=0;for(int x:compact)total+=(x>>>4)+1;
  int[] lengths=new int[total];int k=0;
  for(int x:compact)for(int j=0;j<=(x>>>4);j++){lengths[k++]=x&15;count[x&15]++;}
  int[] offsets=new int[14];for(int i=1;i<13;i++)offsets[i+1]=offsets[i]+count[i];
  symbols=new int[total];for(int i=0;i<total;i++)symbols[offsets[lengths[i]]++]=i;
 }
}
private static final Tree LIT=new Tree(litlen),LEN=new Tree(lenlen),DIST=new Tree(distlen);
private final byte[] input;private int pos,buffer,available;
private Pkware(byte[] input){this.input=input;}
private int bits(int n)throws IOException {
 while(available<n){if(pos==input.length)throw new IOException("Truncated PKWARE stream");buffer|=(input[pos++]&255)<<available;available+=8;}
 int value=buffer&((1<<n)-1);buffer>>>=n;available-=n;return value;
}
private int decode(Tree t)throws IOException {
 int code=0,first=0,index=0;
 for(int len=1;len<=13;len++) {
  code|=bits(1)^1;int count=t.count[len];
  if(code<first+count)return t.symbols[index+code-first];
  index+=count;first=(first+count)<<1;code<<=1;
 }
 throw new IOException("Invalid PKWARE code");
}
static byte[] expand(byte[] input,int wanted)throws IOException {
 Pkware s=new Pkware(input);int coded=s.bits(8),dict=s.bits(8);
 if(coded>1 || dict<4 || dict>6)throw new IOException("Invalid PKWARE header");
 byte[] out=new byte[wanted];int used=0;
 while(true) {
  if(s.bits(1)==0){if(used==wanted)throw new IOException("PKWARE output overflow");out[used++]=(byte)(coded==1?s.decode(LIT):s.bits(8));}
  else {
   int symbol=s.decode(LEN),len=base[symbol]+s.bits(extra[symbol]);
   if(len==519)break;
   int n=len==2?2:dict,distance=(s.decode(DIST)<<n)+s.bits(n)+1;
   if(distance>used || len>wanted-used)throw new IOException("Invalid PKWARE back reference");
   for(int i=0;i<len;i++){out[used]=out[used-distance];used++;}
  }
 }
 if(used!=wanted || s.pos!=input.length)throw new IOException("PKWARE size/trailer mismatch");
 return out;
}
}

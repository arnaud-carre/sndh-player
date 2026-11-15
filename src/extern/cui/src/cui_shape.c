/* Small horizontal OpenType interpreter for the optional Arabic text path. */
#include "cui_shape.h"
#include <string.h>
#include <limits.h>
#define TAG(a,b,c,d) ((uint32_t)(a)<<24|(uint32_t)(b)<<16|(uint32_t)(c)<<8|(d))
typedef uint32_t off;
static unsigned u16(const cui_shape_font *f, off p) {
    return p <= f->size && f->size-p >= 2 ? (unsigned)f->data[p]*256+f->data[p+1] : 0;
}
static int s16(const cui_shape_font *f, off p) { return (int)(int16_t)u16(f,p); }
static off u32(const cui_shape_font *f, off p) {
    return p <= f->size && f->size-p >= 4 ? (off)u16(f,p)*65536+u16(f,p+2) : 0;
}
static off rel(const cui_shape_font *f, off p, off delta) {
    return p && delta && delta < f->size && p < f->size-delta ? p+delta : 0;
}
static off table(const cui_shape_font *f, off sfnt, off tag) {
    unsigned n=u16(f,sfnt+4);
    for (unsigned i=0;i<n;i++) {
        off p=sfnt+12+16*i;
        if (u32(f,p)==tag) {
            off o=u32(f,p+8), len=u32(f,p+12);
            return o && o<=f->size && len<=f->size-o ? o : 0;
        }
    }
    return 0;
}
int cui__shape_font_init(cui_shape_font *f,const void *data,size_t size,int offset) {
    memset(f,0,sizeof *f);
    if (!data || size>INT_MAX || offset<0 || (size_t)offset+12>size) return 0;
    f->data=data; f->size=size;
    f->gsub=table(f,offset,TAG('G','S','U','B'));
    f->gpos=table(f,offset,TAG('G','P','O','S'));
    f->gdef=table(f,offset,TAG('G','D','E','F'));
    f->hmtx=table(f,offset,TAG('h','m','t','x'));
    off hhea=table(f,offset,TAG('h','h','e','a'));
    f->metrics=hhea ? u16(f,hhea+34) : 0;
    off cmap=table(f,offset,TAG('c','m','a','p'));
    int best=0;
    for (unsigned i=0;cmap && i<u16(f,cmap+2);i++) {
        off p=cmap+4+8*i, sub=rel(f,cmap,u32(f,p+4));
        unsigned platform=u16(f,p), enc=u16(f,p+2), format=u16(f,sub);
        int rank=(format==12?2:format==4?1:0);
        if ((platform==0 || (platform==3 && (enc==1 || enc==10))) && rank>best) {
            f->cmap=sub; best=rank;
        }
    }
    return f->cmap && f->metrics && f->hmtx;
}
int cui__shape_map(const cui_shape_font *f,uint32_t cp) {
    off p=f->cmap;
    if (u16(f,p)==12) {
        unsigned lo=0, hi=u32(f,p+12);
        while (lo<hi) {
            unsigned m=lo+(hi-lo)/2; off r=p+16+12*m;
            if (cp<u32(f,r)) hi=m;
            else if (cp>u32(f,r+4)) lo=m+1;
            else return (int)(u32(f,r+8)+cp-u32(f,r));
        }
    } else if (cp<=65535) {
        unsigned n=u16(f,p+6)/2;
        for (unsigned i=0;i<n;i++) {
            off e=p+14+2*i, s=e+2*n+2, d=s+2*n, r=d+2*n;
            if (cp>u16(f,e)) continue;
            if (cp<u16(f,s)) return 0;
            unsigned g=u16(f,r);
            if (g) { g=u16(f,r+g+2*(cp-u16(f,s))); if (!g) return 0; }
            else g=cp;
            return (g+s16(f,d))&65535;
        }
    }
    return 0;
}
static int coverage(const cui_shape_font *f,off p,int glyph) {
    if (!p) return -1;
    unsigned lo=0,hi=u16(f,p+2),fmt=u16(f,p);
    while (lo<hi) {
        unsigned m=lo+(hi-lo)/2; off r=p+4+(fmt==1?2:6)*m;
        unsigned first=u16(f,r),last=fmt==1?first:u16(f,r+2);
        if ((unsigned)glyph<first) hi=m;
        else if ((unsigned)glyph>last) lo=m+1;
        else return fmt==1?(int)m:(int)(u16(f,r+4)+glyph-first);
    }
    return -1;
}
static int klass(const cui_shape_font *f,off p,int glyph) {
    if (!p) return 0;
    if (u16(f,p)==1) {
        int i=glyph-(int)u16(f,p+2);
        return i>=0 && i<(int)u16(f,p+4)?u16(f,p+6+2*i):0;
    }
    unsigned lo=0,hi=u16(f,p+2);
    while (lo<hi) {
        unsigned m=lo+(hi-lo)/2; off r=p+4+6*m;
        if (glyph<(int)u16(f,r)) hi=m;
        else if (glyph>(int)u16(f,r+2)) lo=m+1;
        else return u16(f,r+4);
    }
    return 0;
}
typedef struct {
    cui_shape_font *f; cui_shape_glyph *g;
    int n,cap,rtl,position,failed,operations;
    off layout,list;
} shape;
static int glyph_class(shape *s,int i) {
    off p=s->f->gdef;
    return p?klass(s->f,rel(s->f,p,u16(s->f,p+4)),s->g[i].id):0;
}
static int ignored(shape *s,int i,unsigned flags,off lookup) {
    int cl=glyph_class(s,i);
    if ((cl==1 && (flags&2)) || (cl==2 && (flags&4)) || (cl==3 && (flags&8))) return 1;
    if (cl==3 && s->f->gdef) {
        off p=s->f->gdef;
        if (flags&16) {
            off sets=rel(s->f,p,u16(s->f,p+12));
            unsigned index=u16(s->f,lookup+6+2*u16(s->f,lookup+4));
            if (index>=u16(s->f,sets+2) || coverage(s->f,rel(s->f,sets,u32(s->f,sets+4+4*index)),s->g[i].id)<0) return 1;
        } else if ((flags>>8) && klass(s->f,rel(s->f,p,u16(s->f,p+10)),s->g[i].id)!=(int)(flags>>8)) return 1;
    }
    return 0;
}
static int next(shape *s,int i,int step,unsigned flags,off lookup) {
    do { i+=step; } while (i>=0 && i<s->n && ignored(s,i,flags,lookup));
    return i;
}
static int apply(shape *s,unsigned lookup,int i,int depth);
static int match(shape *s,off values,int count,int *at,int step,int mode,off classes,unsigned flags,off lookup) {
    for (int k=0;k<count;k++) {
        int j=next(s,*at,step,flags,lookup);
        if (j<0 || j>=s->n) return 0;
        int value=u16(s->f,values+2*k),g=s->g[j].id;
        if (mode==0 ? g!=value : mode==1 ? klass(s->f,classes,g)!=value : coverage(s->f,rel(s->f,classes,value),g)<0) return 0;
        *at=j;
    }
    return 1;
}
/* Context formats share the same sequence-lookup records after matching. */
static int context(shape *s,off p,int i,int chained,unsigned flags,off lookup,int depth) {
    cui_shape_font *f=s->f;
    unsigned fmt=u16(f,p),rules=1; off set=0,classes[3]={0,0,0};
    int mode=fmt==3?2:fmt==2?1:0;
    if (fmt!=3) {
        int cov=coverage(f,rel(f,p,u16(f,p+2)),s->g[i].id);
        if (cov<0) return 0;
        off q=p+4;
        if (fmt==2) {
            for (int k=0;k<(chained?3:1);k++,q+=2) classes[k]=rel(f,p,u16(f,q));
            cov=klass(f,classes[chained?1:0],s->g[i].id);
        }
        unsigned n=u16(f,q);
        if (cov>=(int)n) return 0;
        set=rel(f,p,u16(f,q+2+2*cov));
        if (!set) return 0;
        rules=u16(f,set);
    }
    for (unsigned r=0;r<rules;r++) {
        off q=fmt==3?p+2:rel(f,set,u16(f,set+2+2*r));
        int at=i,n,positions[256]; unsigned records=0;
        if (chained) {
            n=u16(f,q); q+=2;
            at=i;
            if (!match(s,q,n,&at,-1,mode,fmt==3?p:classes[0],flags,lookup)) continue;
            q+=2*n;
            n=u16(f,q); q+=2;
        } else { n=u16(f,q); q+=2; }
        if (n<1 || n>256) continue;
        if (!chained) { records=u16(f,q); q+=2; }
        if (fmt==3) {
            if (coverage(f,rel(f,p,u16(f,q)),s->g[i].id)<0) continue;
            q+=2;
        }
        positions[0]=i; at=i; int ok=1;
        for (int k=1;k<n;k++) {
            if (!match(s,q,1,&at,1,mode,fmt==3?p:classes[chained?1:0],flags,lookup)) { ok=0; break; }
            positions[k]=at; q+=2;
        }
        if (!ok) continue;
        if (chained) {
            int ahead=u16(f,q); q+=2;
            if (!match(s,q,ahead,&at,1,mode,fmt==3?p:classes[2],flags,lookup)) continue;
            q+=2*ahead; records=u16(f,q); q+=2;
        }
        for (unsigned k=0;k<records;k++) {
            unsigned seq=u16(f,q+4*k),li=u16(f,q+4*k+2);
            if (seq>=(unsigned)n) continue;
            int before=s->n,pos=positions[seq];
            apply(s,li,pos,depth+1);
            int delta=s->n-before;
            for (int j=seq+1;j<n;j++) positions[j]+=delta;
        }
        return 1;
    }
    return 0;
}
static int value_size(unsigned format) {
    int n=0; for (;format;format>>=1) n+=format&1; return 2*n;
}
static void value(shape *s,int i,off p,unsigned fmt) {
    int *v[4]={&s->g[i].x,&s->g[i].y,&s->g[i].advance,0};
    for (unsigned k=0;k<8;k++) if (fmt&(1u<<k)) {
        if (k<3) *v[k]+=s16(s->f,p);
        p+=2;
    }
}
static int anchor(shape *s,off p,int *x,int *y) {
    if (!p) return 0;
    *x=s16(s->f,p+2); *y=s16(s->f,p+4); return 1;
}
static int subtable(shape *s,off p,unsigned type,int i,unsigned flags,off lookup,int depth) {
    cui_shape_font *f=s->f;
    if (depth>12 || !p) return 0;
    unsigned fmt=u16(f,p);
    if ((!s->position && type==7) || (s->position && type==9)) {
        if (fmt!=1) return 0;
        return subtable(s,rel(f,p,u32(f,p+4)),u16(f,p+2),i,flags,lookup,depth+1);
    }
    if ((!s->position && (type==5 || type==6)) || (s->position && (type==7 || type==8)))
        return context(s,p,i,type==(s->position?8u:6u),flags,lookup,depth);
    int ci=coverage(f,rel(f,p,u16(f,p+2)),s->g[i].id);
    if (ci<0) return 0;
    if (!s->position) {
        if (type==1) {
            if (fmt==1) s->g[i].id=(s->g[i].id+s16(f,p+4))&65535;
            else if (fmt==2 && ci<(int)u16(f,p+4)) s->g[i].id=u16(f,p+6+2*ci);
            return 1;
        }
        if (type==2 || type==3) {
            if (fmt!=1 || ci>=(int)u16(f,p+4)) return 0;
            off q=rel(f,p,u16(f,p+6+2*ci)); int n=u16(f,q);
            if (type==3) { if (n) s->g[i].id=u16(f,q+2); return n!=0; }
            if (n>s->cap-s->n+1) { s->failed=1; return 0; }
            memmove(s->g+i+n,s->g+i+1,(size_t)(s->n-i-1)*sizeof *s->g);
            cui_shape_glyph old=s->g[i];
            for (int k=0;k<n;k++) { s->g[i+k]=old; s->g[i+k].id=u16(f,q+2+2*k); }
            s->n+=n-1; return n?n:1;
        }
        if (type==4 && fmt==1 && ci<(int)u16(f,p+4)) {
            off set=rel(f,p,u16(f,p+6+2*ci));
            for (unsigned k=0;k<u16(f,set);k++) {
                off q=rel(f,set,u16(f,set+2+2*k)); int n=u16(f,q+2),at=i;
                int positions[256]; positions[0]=i;
                if (n<1 || n>256) continue;
                int ok=1;
                for (int j=1;j<n;j++) {
                    at=next(s,at,1,flags,lookup);
                    if (at>=s->n || s->g[at].id!=(int)u16(f,q+2+2*j) || s->g[at].form!=s->g[i].form) { ok=0; break; }
                    positions[j]=at;
                }
                if (!ok) continue;
                s->g[i].id=u16(f,q); s->g[i].ligature=n;
                for (int j=n-1;j>0;j--) {
                    int atj=positions[j];
                    for (int a=positions[j-1]+1;a<atj;a++) s->g[a].component=j;
                    memmove(s->g+atj,s->g+atj+1,(size_t)(s->n-atj-1)*sizeof *s->g); s->n--;
                }
                return 1;
            }
        }
        return 0;
    }
    if (type==1) {
        unsigned vf=u16(f,p+4); off q=p+6;
        if (fmt==2) q+=2+ci*value_size(vf);
        value(s,i,q,vf); return 1;
    }
    if (type==2) {
        unsigned v1=u16(f,p+4),v2=u16(f,p+6); int j=next(s,i,1,flags,lookup);
        if (j>=s->n) return 0;
        off q=0;
        if (fmt==1 && ci<(int)u16(f,p+8)) {
            off set=rel(f,p,u16(f,p+10+2*ci)); int sz=2+value_size(v1)+value_size(v2);
            for (unsigned k=0;k<u16(f,set);k++) {
                off r=set+2+k*sz;
                if (s->g[j].id==(int)u16(f,r)) { q=r+2; break; }
            }
        } else if (fmt==2) {
            int c1=klass(f,rel(f,p,u16(f,p+8)),s->g[i].id),c2=klass(f,rel(f,p,u16(f,p+10)),s->g[j].id);
            if (c1<(int)u16(f,p+12) && c2<(int)u16(f,p+14)) q=p+16+(c1*u16(f,p+14)+c2)*(value_size(v1)+value_size(v2));
        }
        if (!q) return 0;
        value(s,i,q,v1); value(s,j,q+value_size(v1),v2); return v2?2:1;
    }
    if ((type==4 || type==5 || type==6) && fmt==1) {
        int j=i-1;
        if (type==6) {
            while (j>=0 && ignored(s,j,flags,lookup)) j--;
            if (j>=0 && glyph_class(s,j)!=3) return 0;
        } else while (j>=0 && glyph_class(s,j)==3) j--;
        if (j<0) return 0;
        int bi=coverage(f,rel(f,p,u16(f,p+4)),s->g[j].id);
        off marks=rel(f,p,u16(f,p+8)),bases=rel(f,p,u16(f,p+10));
        unsigned classes=u16(f,p+6),mc=u16(f,marks+2+4*ci);
        if (bi<0 || ci>=(int)u16(f,marks) || mc>=classes || bi>=(int)u16(f,bases)) return 0;
        off a=0;
        if (type==5) {
            off attach=rel(f,bases,u16(f,bases+2+2*bi));
            int n=u16(f,attach),component=s->g[i].component?s->g[i].component-1:n-1;
            if (component<0 || component>=n) return 0;
            a=rel(f,attach,u16(f,attach+2+2*(component*classes+mc)));
        } else a=rel(f,bases,u16(f,bases+2+2*(bi*classes+mc)));
        int mx,my,bx,by;
        if (!anchor(s,rel(f,marks,u16(f,marks+4+4*ci)),&mx,&my) || !anchor(s,a,&bx,&by)) return 0;
        int distance=0;
        for (int k=j;k<i;k++) distance+=s->g[k].advance;
        s->g[i].x=s->g[j].x+bx-mx+(s->rtl?s->g[i].advance: -distance);
        s->g[i].y=s->g[j].y+by-my;
        return 1;
    }
    return 0;
}
static int apply(shape *s,unsigned index,int i,int depth) {
    if (depth>12 || i<0 || i>=s->n || index>=u16(s->f,s->list)) return 0;
    if (++s->operations>1000000) { s->failed=1; return 0; }
    off p=rel(s->f,s->list,u16(s->f,s->list+2+2*index));
    unsigned flags=u16(s->f,p+2);
    if (!p || ignored(s,i,flags,p)) return 0;
    for (unsigned j=0;j<u16(s->f,p+4);j++) {
        int n=subtable(s,rel(s->f,p,u16(s->f,p+6+2*j)),u16(s->f,p),i,flags,p,depth);
        if (n) return n;
    }
    return 0;
}
static off language(cui_shape_font *f,off layout,off script) {
    off list=rel(f,layout,u16(f,layout+4)),found=0,default_script=0;
    for (unsigned k=0;k<u16(f,list);k++) {
        off r=list+2+6*k,tag=u32(f,r);
        if (tag==script) found=rel(f,list,u16(f,r+4));
        if (tag==TAG('D','F','L','T')) default_script=rel(f,list,u16(f,r+4));
    }
    if (!found) found=default_script;
    return found?rel(f,found,u16(f,found)):0;
}
static void feature(shape *s,off lang,off tag,int form) {
    cui_shape_font *f=s->f;
    off features=rel(f,s->layout,u16(f,s->layout+6));
    if (!lang) return;
    for (unsigned k=0;k<u16(f,lang+4);k++) {
        unsigned index=u16(f,lang+6+2*k);
        if (index>=u16(f,features)) continue;
        off r=features+2+6*index;
        if (u32(f,r)!=tag) continue;
        off p=rel(f,features,u16(f,r+4));
        for (unsigned l=0;l<u16(f,p+2);l++) {
            unsigned lookup=u16(f,p+4+2*l);
            for (int i=0;i<s->n;) {
                if (form<0 || s->g[i].form==form) { int n=apply(s,lookup,i,0); i+=n>0?n:1; }
                else i++;
            }
        }
    }
}
#include "cui_joining.h"
static int combining(uint32_t cp) {
    int lo=0,hi=(int)(sizeof cui_combining/sizeof *cui_combining);
    while (lo<hi) {
        int m=lo+(hi-lo)/2;
        if (cp<cui_combining[m].cp) hi=m;
        else if (cp>cui_combining[m].cp) lo=m+1;
        else return cui_combining[m].value;
    }
    return 0;
}
static int modifier(uint32_t cp) {
    /* Unicode TR53 modifier combining marks, reordered closest to the base. */
    return cp==0x654 || cp==0x655 || cp==0x658 || cp==0x6dc || cp==0x6e3 ||
           cp==0x6e7 || cp==0x6e8 || cp==0x8ca || cp==0x8cb || cp==0x8cd ||
           cp==0x8ce || cp==0x8cf || cp==0x8d3 || cp==0x8f3;
}
static int normalize(cui_shape_font *f,cui_shape_glyph *g,int n,int arabic) {
    /* Canonical order, then compose only when the font has the composed glyph. */
    for (int i=0;i<n;i++) {
        int c=combining(g[i].codepoint);
        g[i].form=arabic?(c==33?27:c>=27 && c<33?c+1:c):c;
    }
    for (int i=1;i<n;i++) {
        int c=g[i].form;
        for (int j=i;c && j>0 && g[j-1].form>c;j--) {
            cui_shape_glyph t=g[j]; g[j]=g[j-1]; g[j-1]=t;
        }
    }
    if (arabic) for (int start=0;start<n;) {
        if (!g[start].form) { start++; continue; }
        int end=start+1;
        while (end<n && g[end].form) end++;
        int front=start;
        for (int c=220;c<=230;c+=10) {
            int at=front;
            while (at<end && g[at].form<c) at++;
            int last=at;
            while (last<end && g[last].form==c && modifier(g[last].codepoint)) last++;
            for (int k=at;k<last;k++) {
                cui_shape_glyph t=g[k]; t.form=c==220?25:26;
                memmove(g+front+1,g+front,(size_t)(k-front)*sizeof *g);
                g[front++]=t;
            }
        }
        start=end;
    }
    int base=-1,last=0;
    for (int i=0;i<n;i++) {
        int c=g[i].form,composed=0;
        if (base>=0 && c && (last<c || !last)) {
            for (unsigned k=0;k<sizeof cui_compositions/sizeof *cui_compositions;k++)
                if (g[base].codepoint==cui_compositions[k].base && g[i].codepoint==cui_compositions[k].mark && cui__shape_map(f,cui_compositions[k].cp)) {
                    g[base].codepoint=cui_compositions[k].cp;
                    memmove(g+i,g+i+1,(size_t)(n-i-1)*sizeof *g); n--; i--; composed=1; break;
                }
        }
        if (composed) continue;
        if (!c) base=i;
        last=c;
    }
    return n;
}
static int joining(uint32_t cp) {
    int lo=0,hi=(int)(sizeof cui_joining/sizeof *cui_joining);
    while (lo<hi) {
        int m=lo+(hi-lo)/2;
        if (cp<cui_joining[m].first) hi=m;
        else if (cp>cui_joining[m].first+cui_joining[m].span) lo=m+1;
        else return cui_joining[m].type;
    }
    return 0;
}
int cui__shape(cui_shape_font *f,cui_shape_glyph *g,int count,int capacity,uint32_t script,int rtl,int tabular) {
    if (count<0 || count>capacity || !f->cmap) return -1;
    count=normalize(f,g,count,script==TAG('a','r','a','b'));
    shape s={f,g,count,capacity,rtl,0,0,0,0,0};
    int prev=-1,prev_type=0;
    for (int i=0;i<count;i++) {
        g[i].id=cui__shape_map(f,g[i].codepoint); g[i].x=g[i].y=0;
        g[i].form=-1; g[i].ligature=g[i].component=0;
        int jt=joining(g[i].codepoint);
        if (script==TAG('a','r','a','b') && jt!=4) {
            if (jt) g[i].form=0;
            if (prev>=0 && (prev_type&1) && (jt&2)) {
                g[prev].form=g[prev].form==1?2:3; g[i].form=1;
            }
            prev=i; prev_type=jt;
        }
    }
    static const off tags[]={TAG('r','t','l','a'),TAG('r','t','l','m'),TAG('s','t','c','h'),TAG('c','c','m','p'),TAG('l','o','c','l'),
        TAG('i','s','o','l'),TAG('f','i','n','a'),TAG('m','e','d','i'),TAG('i','n','i','t'),TAG('r','l','i','g'),TAG('c','a','l','t'),
        TAG('r','c','l','t'),TAG('l','i','g','a'),TAG('c','l','i','g'),TAG('m','s','e','t')};
    s.layout=f->gsub; s.list=rel(f,s.layout,u16(f,s.layout+8));
    off lang=language(f,s.layout,script);
    for (int k=0;k<(int)(sizeof tags/sizeof *tags);k++) {
        if (k<2 && !rtl) continue;
        if (k==9) for (int i=0;i<s.n;i++) g[i].form=-1;
        feature(&s,lang,tags[k],k>=5 && k<=8?k-5:-1);
    }
    if (tabular) feature(&s,lang,TAG('t','n','u','m'),-1);
    for (int i=0;i<s.n;i++) {
        int glyph=g[i].id;
        g[i].advance=u16(f,f->hmtx+4*(glyph<f->metrics?glyph:f->metrics-1));
        if (glyph_class(&s,i)==3) g[i].advance=0;
    }
    s.position=1; s.layout=f->gpos; s.list=rel(f,s.layout,u16(f,s.layout+8)); lang=language(f,s.layout,script);
    static const off pos[]={TAG('c','u','r','s'),TAG('k','e','r','n'),TAG('m','a','r','k'),TAG('m','k','m','k'),TAG('d','i','s','t')};
    for (int k=0;k<5;k++) feature(&s,lang,pos[k],-1);
    for (int i=0;i<s.n;i++) if ((g[i].codepoint>=0x200b && g[i].codepoint<=0x200f) || (g[i].codepoint>=0x202a && g[i].codepoint<=0x202e) || (g[i].codepoint>=0x2066 && g[i].codepoint<=0x2069)) {
        g[i].id=cui__shape_map(f,' '); g[i].advance=g[i].x=g[i].y=0;
    }
    if (rtl) for (int i=0;i<s.n/2;i++) { cui_shape_glyph t=g[i]; g[i]=g[s.n-1-i]; g[s.n-1-i]=t; }
    return s.failed?-1:s.n;
}

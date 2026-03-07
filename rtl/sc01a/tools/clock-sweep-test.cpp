#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <initializer_list>

static double m_sclock = 0;
static double m_cclock = 0;
static void set_clocks(double mc) { m_sclock = mc/18.0; m_cclock = mc/36.0; }
static const double FP_SCALE = 32768.0;

static double bits_to_caps(uint32_t v, std::initializer_list<double> caps) {
    double t=0; for(double d:caps){if(v&1)t+=d;v>>=1;} return t;
}

// --- Filter builders (MAME original formulas) ---

static void build_standard(double*a,double*b,
    double c1t,double c1b,double c2t,double c2b,double c3,double c4){
    double k0=c1t/(m_cclock*c1b);
    double k1=c4*c2t/(m_cclock*c1b*c3);
    double k2=c4*c2b/(m_cclock*m_cclock*c1b*c3);
    double fp=sqrt(fabs(k0*k1-k2))/(2*M_PI*k2);
    double zc=2*M_PI*fp/tan(M_PI*fp/m_sclock);
    double m0=zc*k0,m1=zc*k1,m2=zc*zc*k2,b0=1+m1+m2;
    a[0]=(1+m0)/b0;a[1]=(3+m0)/b0;a[2]=(3-m0)/b0;a[3]=(1-m0)/b0;
    b[0]=1;b[1]=(3+m1-m2)/b0;b[2]=(3-m1-m2)/b0;b[3]=(1-m1+m2)/b0;
}

static void build_lowpass(double*a,double*b,double c1t,double c1b){
    double k=c1b/(m_cclock*c1t)*(150.0/4000.0);
    double fp=1/(2*M_PI*k);
    double zc=2*M_PI*fp/tan(M_PI*fp/m_sclock);
    double m=zc*k,b0=1+m;
    a[0]=1/b0; b[0]=1; b[1]=(1-m)/b0;
}

// MAME original — has m_cclock in k1 and k2
static void build_noise(double*a,double*b,
    double c1,double c2t,double c2b,double c3,double c4){
    double k0=c2t*c3*c2b/c4;
    double k1=c2t*(m_cclock*c2b);          // <-- cclock here
    double k2=c1*c2t*c3/(m_cclock*c4);     // <-- cclock here
    double fp=sqrt(1/k2)/(2*M_PI);
    double zc=2*M_PI*fp/tan(M_PI*fp/m_sclock);
    double m0=zc*k0,m1=zc*k1,m2=zc*zc*k2,b0=1+m1+m2;
    a[0]=m0/b0;a[1]=0;a[2]=-m0/b0;
    b[0]=1;b[1]=(2-2*m2)/b0;b[2]=(1-m1+m2)/b0;
}

// --- Per-filter max diff vs reference ---
static double maxdiff(double*r,double*s,int n){
    double mx=0;
    for(int i=0;i<n;i++){double d=fabs(r[i]-s[i])*FP_SCALE;if(d>mx)mx=d;}
    return mx;
}

struct PerFilter { double f1,f2v,f3,f4,fx,fn; };

static PerFilter compute_diffs(double mainclock,
    double ref_f1[16][8], double ref_f2v[16][32][8],
    double ref_f3[16][8], double ref_f4[8],
    double ref_fx[3],     double ref_fn[3])
{
    set_clocks(mainclock);
    PerFilter r={};

    double a[4],b[4];

    // F1
    for(int i=0;i<16;i++){
        double c3=2280+bits_to_caps(i,{2546,4973,9861,19724});
        build_standard(a,b,11247,11797,949,52067,c3,166272);
        r.f1=fmax(r.f1, maxdiff(a,ref_f1[i],4)+maxdiff(b,ref_f1[i]+4,4));
    }

    // F2V
    for(int q=0;q<16;q++){
        double c2t=829+bits_to_caps(q,{1390,2965,5875,11297});
        for(int f=0;f<32;f++){
            double c3=2352+bits_to_caps(f,{833,1663,3164,6327,12654});
            build_standard(a,b,24840,29154,c2t,38180,c3,34270);
            r.f2v=fmax(r.f2v, maxdiff(a,ref_f2v[q][f],4)+maxdiff(b,ref_f2v[q][f]+4,4));
        }
    }

    // F3
    for(int i=0;i<16;i++){
        double c3=8480+bits_to_caps(i,{2226,4485,9056,18111});
        build_standard(a,b,0,17594,868,18828,c3,50019);
        r.f3=fmax(r.f3, maxdiff(a,ref_f3[i],4)+maxdiff(b,ref_f3[i]+4,4));
    }

    // F4
    build_standard(a,b,0,28810,1165,21457,8558,7289);
    r.f4=maxdiff(a,ref_f4,4)+maxdiff(b,ref_f4+4,4);

    // FX
    double ax[1],bx[2];
    build_lowpass(ax,bx,1122,23131);
    r.fx=maxdiff(ax,ref_fx,1)+maxdiff(bx,ref_fx+1,2);

    // FN
    double an[3],bn[3];
    build_noise(an,bn,15500,14854,8450,9523,14083);
    double fn_flat[6]={an[0],an[1],an[2],bn[0],bn[1],bn[2]};
    r.fn=maxdiff(fn_flat,ref_fn,6);

    return r;
}

int main(){
    // Compute reference at nominal 720kHz
    set_clocks(720000);

    static double ref_f1[16][8], ref_f2v[16][32][8], ref_f3[16][8];
    static double ref_f4[8], ref_fx[3], ref_fn[6];

    double a[4],b[4];
    for(int i=0;i<16;i++){
        double c3=2280+bits_to_caps(i,{2546,4973,9861,19724});
        build_standard(a,b,11247,11797,949,52067,c3,166272);
        for(int j=0;j<4;j++){ref_f1[i][j]=a[j];ref_f1[i][j+4]=b[j];}
    }
    for(int q=0;q<16;q++){
        double c2t=829+bits_to_caps(q,{1390,2965,5875,11297});
        for(int f=0;f<32;f++){
            double c3=2352+bits_to_caps(f,{833,1663,3164,6327,12654});
            build_standard(a,b,24840,29154,c2t,38180,c3,34270);
            for(int j=0;j<4;j++){ref_f2v[q][f][j]=a[j];ref_f2v[q][f][j+4]=b[j];}
        }
    }
    for(int i=0;i<16;i++){
        double c3=8480+bits_to_caps(i,{2226,4485,9056,18111});
        build_standard(a,b,0,17594,868,18828,c3,50019);
        for(int j=0;j<4;j++){ref_f3[i][j]=a[j];ref_f3[i][j+4]=b[j];}
    }
    build_standard(a,b,0,28810,1165,21457,8558,7289);
    for(int j=0;j<8;j++) ref_f4[j]=(j<4)?a[j]:b[j-4];

    double ax[1],bx[2];
    build_lowpass(ax,bx,1122,23131);
    ref_fx[0]=ax[0]; ref_fx[1]=bx[0]; ref_fx[2]=bx[1];

    double an[3],bn[3];
    build_noise(an,bn,15500,14854,8450,9523,14083);
    ref_fn[0]=an[0];ref_fn[1]=an[1];ref_fn[2]=an[2];
    ref_fn[3]=bn[0];ref_fn[4]=bn[1];ref_fn[5]=bn[2];

    // Header
    printf("Votrax SC-01A — Clock invariance check per filter (prewarped bilinear)\n");
    printf("Reference: mainclock=720000 Hz. All values = max diff in LSB (s2.15)\n\n");
    printf("%-10s %-8s %-8s | %-8s %-8s %-8s %-8s %-8s %-8s\n",
           "main(Hz)","sc(Hz)","Nq(Hz)","F1","F2V","F3","F4","FX","FN");
    printf("%-90s\n","-------------------------------------------------------------------------------------------");

    for(double mc=450000; mc<=1100001; mc+=50000){
        PerFilter d = compute_diffs(mc,ref_f1,ref_f2v,ref_f3,ref_f4,ref_fx,ref_fn);
        printf("%-10.0f %-8.0f %-8.0f | %-8.3f %-8.3f %-8.3f %-8.3f %-8.3f %-8.3f\n",
               mc, mc/18, mc/36,
               d.f1, d.f2v, d.f3, d.f4, d.fx, d.fn);
    }
    return 0;
}

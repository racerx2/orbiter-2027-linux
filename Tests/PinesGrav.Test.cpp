// Independent validation of Orbiter's Pines gravity with real and synthetic models.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>
#include <unistd.h>
#include "Vecmat.h"
#include "PinesGrav.h"
namespace fs=std::filesystem;
struct Model {
 double radius=0,mu=0;
 std::vector<std::vector<double>> c,s;
};
static Model ReadModel(const std::string &file,int N) {
 Model g;
 g.c.resize(N+1);g.s.resize(N+1);
 for(int n=0;n<=N;n++){g.c[n].resize(n+1);g.s[n].resize(n+1);}
 std::ifstream f(file);
 std::string line;
 REQUIRE(bool(std::getline(f,line)));
 REQUIRE(std::sscanf(line.c_str()," %lf , %lf",&g.radius,&g.mu)==2);
 while(std::getline(f,line)){
  int n,m;double c,s;
  if(std::sscanf(line.c_str()," %d , %d , %lf , %lf",&n,&m,&c,&s)!=4)continue;
  if(n>N)break;
  REQUIRE(n>=1);REQUIRE(m>=0);REQUIRE(m<=n);
  g.c[n][m]=c;g.s[n][m]=s;
 }
 return g;
}
static double Potential(const Model &g,Vector v) {
 const int N=int(g.c.size())-1;
 const double r=sqrt(v.x*v.x+v.y*v.y+v.z*v.z),u=v.z/r,w=sqrt(std::max(0.0,1-u*u));
 const double lon=atan2(v.y,v.x);
 std::vector<std::vector<double>> p(N+1,std::vector<double>(N+1));
 p[0][0]=1;
 for(int m=1;m<=N;m++)p[m][m]=(2*m-1)*w*p[m-1][m-1];
 for(int m=0;m<N;m++)p[m+1][m]=(2*m+1)*u*p[m][m];
 for(int m=0;m<=N;m++)for(int n=m+2;n<=N;n++)
  p[n][m]=((2*n-1)*u*p[n-1][m]-(n+m-1)*p[n-2][m])/(n-m);
 double result=0,power=pow(g.radius/r,2);
 for(int n=2;n<=N;n++){
  double sum=0;
  for(int m=0;m<=n;m++){
   const double norm=sqrt((m?2.0:1.0)*(2*n+1)*
                    exp(lgamma(n-m+1.0)-lgamma(n+m+1.0)));
   sum+=norm*p[n][m]*(g.c[n][m]*cos(m*lon)+g.s[n][m]*sin(m*lon));
  }
  result+=(g.mu/r)*power*sum;
  power*=g.radius/r;
 }
 return result;
}
static Vector Gradient(const Model &g,Vector v) {
 const double h=0.02; // independent 20-m finite difference; position in km
 return Vector((Potential(g,Vector(v.x+h,v.y,v.z))-Potential(g,Vector(v.x-h,v.y,v.z)))/(2*h),
               (Potential(g,Vector(v.x,v.y+h,v.z))-Potential(g,Vector(v.x,v.y-h,v.z)))/(2*h),
               (Potential(g,Vector(v.x,v.y,v.z+h))-Potential(g,Vector(v.x,v.y,v.z-h)))/(2*h));
}
static double Distance(Vector a,Vector b) {
 return sqrt(pow(a.x-b.x,2)+pow(a.y-b.y,2)+pow(a.z-b.z,2));
}
struct TmpFile {
 fs::path path;
 TmpFile(){char name[]="/tmp/orb_grav_XXXXXX";int fd=mkstemp(name);REQUIRE(fd>=0);close(fd);path=name;}
 ~TmpFile(){std::error_code ec;fs::remove(path,ec);}
};
TEST_CASE("Pines matches isolated analytic J2", "[gravity]") {
 const double radius=6378.1363,mu=398600.4415,J2=0.00108262668;
 TmpFile temp;
 {
  std::ofstream f(temp.path);f<<std::setprecision(17);
  f<<radius<<", "<<mu<<", 0, 2, 2, 1, 0, 0\n";
  f<<"1, 0, 0, 0, 0, 0\n1, 1, 0, 0, 0, 0\n";
  f<<"2, 0, "<<-J2/sqrt(5.0)<<", 0, 0, 0\n";
  f<<"2, 1, 0, 0, 0, 0\n2, 2, 0, 0, 0, 0\n";
 }
 PinesGravProp p(nullptr);int loaded=0,total=0;auto path=temp.path.string();
 REQUIRE(p.readGravModel(path.data(),2,loaded,total)==0);
 for(const Vector v:{Vector(7000,0,0),Vector(0,0,7000),Vector(4600,5100,2100),Vector(20,-35,-7600)}) {
  double r2=v.x*v.x+v.y*v.y+v.z*v.z,z2=v.z*v.z/r2;
  double k=1.5*mu*J2*radius*radius/(r2*r2*sqrt(r2));
  Vector expected(k*v.x*(5*z2-1),k*v.y*(5*z2-1),k*v.z*(5*z2-3));
  INFO("test position "<<v.x<<","<<v.y<<","<<v.z);
  CHECK(Distance(p.GetPinesGrav(v,2,2),expected)<2e-12); // km/s^2
 }
}
TEST_CASE("Pines matches independent potential gradient for six gravity files","[gravity]"){
 const char *models[]={"egm96_to360.tab","jgl165p1.sha","jgmro_120f_sha.tab",
                      "jgmess_160a_sha.tab","mod_shgj120p.a01","JGDWN_VES20H_SHA.TAB"};
 for(const char *name:models)for(int degree:{2,10}){
  std::string file=std::string("GravityModels/")+name;
  Model g=ReadModel(file,degree);
  PinesGravProp p(nullptr);int loaded=0,total=0;
  REQUIRE(p.readGravModel(file.data(),degree,loaded,total)==0);
  REQUIRE(p.GetCoeffCutoff()==unsigned(degree));
  double r=g.radius+400;
  for(const Vector unit:{Vector(1,0,0),Vector(0.4,0.8,0.4472135955),Vector(0.002,0.003,0.9999935)}){
   Vector v(r*unit.x,r*unit.y,r*unit.z),actual=p.GetPinesGrav(v,degree,degree),expected=Gradient(g,v);
   double delta=Distance(actual,expected);
   double mag=Distance(expected,Vector(0,0,0));
   INFO("file "<<file<<" degree "<<degree<<" delta "<<delta<<" magnitude "<<mag);
   CHECK(std::isfinite(delta));
   CHECK(delta<std::max(2e-11,mag*5e-5)); // km/s^2
  }
 }
}
TEST_CASE("Pines rejects truncated model files","[gravity]"){
 TmpFile temp;
 {std::ofstream f(temp.path);f<<"6378, 398600, 0, 2, 2, 1, 0, 0\n1, 0, 0, 0, 0, 0\n";}
 PinesGravProp p(nullptr);int loaded=0,total=0;auto path=temp.path.string();
 CHECK(p.readGravModel(path.data(),2,loaded,total)!=0);
}

TEST_CASE("Pines full-resolution gravity models stay finite near the poles", "[gravity]") {
    struct FullModel { const char *file; int cutoff; double radius; };
    const FullModel models[]={
        {"egm96_to360.tab",360,6378.1363},
        {"jgl165p1.sha",165,1738.0},
        {"jgmro_120f_sha.tab",120,3396.0},
        {"jgmess_160a_sha.tab",160,2440.0},
        {"mod_shgj120p.a01",120,6051.0},
        {"JGDWN_VES20H_SHA.TAB",20,265.0},
    };
    for(const FullModel &m:models){
        std::string path=std::string("GravityModels/")+m.file;
        PinesGravProp p(nullptr);int loaded=0,total=0;
        REQUIRE(p.readGravModel(path.data(),m.cutoff,loaded,total)==0);
        REQUIRE(p.GetCoeffCutoff()==unsigned(m.cutoff));
        INFO(m.file);
        for(double alt:{10.,100.,400.,2000.}){
            double r=m.radius+alt;
            for(const Vector axis:{Vector(1,0,0),Vector(0.001,0.002,0.9999975),
                                   Vector(.26726124,.53452248,.80178372)}){
                Vector a=p.GetPinesGrav(Vector(r*axis.x,r*axis.y,r*axis.z),m.cutoff,m.cutoff);
                CHECK(std::isfinite(a.x));CHECK(std::isfinite(a.y));CHECK(std::isfinite(a.z));
                // A nonspherical correction cannot plausibly be many times the
                // body's central monopole acceleration at exterior locations.
                double aMagnitude=Distance(a,Vector(0,0,0));
                CHECK(aMagnitude<1.0); // km/s^2, deliberately loose sanity bound
            }
        }
    }
}

TEST_CASE("Pines releases model file descriptors", "[gravity]") {
    const fs::path fdpath("/proc/self/fd");
    if (!fs::exists(fdpath)) SKIP("Linux procfs is not mounted");
    auto countFds=[&](){ size_t count=0;for(auto &f:fs::directory_iterator(fdpath))++count;return count;};
    const size_t before=countFds();
    for(int i=0;i<60;i++){
        PinesGravProp p(nullptr);int loaded=0,total=0;
        std::string path="GravityModels/egm96_to360.tab";
        REQUIRE(p.readGravModel(path.data(),10,loaded,total)==0);
    }
    CHECK(countFds()<=before+2);
}

TEST_CASE("Pines loader rejects invalid header and reordered coefficients", "[gravity]") {
    TmpFile temp;
    PinesGravProp p(nullptr);int loaded=0,total=0;
    auto name=temp.path.string();
    {
        std::ofstream f(temp.path);f<<"6378, 398600, 0, 2, 2\n";
    }
    CHECK(p.readGravModel(name.data(),2,loaded,total)==3);
    {
        std::ofstream f(temp.path);
        f<<"6378, 398600, 0, 2, 2, 1, 0, 0\n";
        f<<"1, 1, 0, 0, 0, 0\n"; // row (1,0) is absent
        f<<"1, 0, 0, 0, 0, 0\n";
        f<<"2, 0, 0, 0, 0, 0\n2, 1, 0, 0, 0, 0\n2, 2, 0, 0, 0, 0\n";
    }
    CHECK(p.readGravModel(name.data(),2,loaded,total)==4);
}

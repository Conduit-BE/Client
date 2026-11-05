#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
int main(int argc,char**argv){
 if(argc<2)return 2;
 std::ifstream in(argv[1],std::ios::binary);
 std::vector<char>b{std::istreambuf_iterator<char>(in),{}};
 auto u32=[&](std::size_t p){std::uint32_t v;std::memcpy(&v,b.data()+p,4);return v;};
 auto pe=u32(0x3c);std::uint16_t n,optional;std::memcpy(&n,&b[pe+6],2);std::memcpy(&optional,&b[pe+20],2);
 struct Section{std::uint32_t rva,size,raw;};std::vector<Section>s;
 for(unsigned i=0;i<n;++i){auto p=pe+24+optional+40*i;s.push_back({u32(p+12),u32(p+16),u32(p+20)});}
 for(int k=2;k<argc;++k){std::string needle=argv[k]; if(needle.starts_with("rva:")){
 auto target=std::stoul(needle.substr(4),nullptr,16); auto text=s.front();
 std::cout<<needle<<"\n";for(std::size_t at=text.raw;at+4<text.raw+text.size;++at){std::int32_t d;std::memcpy(&d,&b[at],4);if(std::int64_t(at-text.raw+text.rva+4)+d==target)std::cout<<" displacement_rva="<<std::hex<<at-text.raw+text.rva<<"\n";}continue; }
 for(std::size_t p=0;p+needle.size()<b.size();++p){if(std::memcmp(&b[p],needle.data(),needle.size())!=0)continue;
 std::uint32_t target=0;for(auto q:s)if(p>=q.raw&&p<q.raw+q.size)target=p-q.raw+q.rva;
 if(!target)continue;
 std::cout<<needle<<" offset="<<std::hex<<p<<" rva="<<target<<"\n";
 auto text=s.front();
 for(std::size_t at=text.raw;at+4<text.raw+text.size;++at){
 std::int32_t d;std::memcpy(&d,&b[at],4);auto dst=std::int64_t(at-text.raw+text.rva+4)+d;
 if(dst==target){std::cout<<"  displacement offset="<<at<<" rva="<<at-text.raw+text.rva<<" bytes=";
 for(std::size_t x=at-4;x<at+8;++x)std::cout<<unsigned(static_cast<unsigned char>(b[x]))<<' ';std::cout<<"\n";}
 }
 }
 }
}

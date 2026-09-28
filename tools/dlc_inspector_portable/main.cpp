#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static constexpr size_t kHeaderSize = 0x971A;
static constexpr size_t kMetaOff = 0x344;
static constexpr size_t kMetaSize = 0x93D6;

static uint32_t be32(const std::vector<uint8_t>& b, size_t o) {
  return (uint32_t(b[o]) << 24) | (uint32_t(b[o+1]) << 16) |
         (uint32_t(b[o+2]) << 8) | uint32_t(b[o+3]);
}
static uint64_t be64(const std::vector<uint8_t>& b, size_t o) {
  return (uint64_t(be32(b,o)) << 32) | be32(b,o+4);
}
static std::string hex8(uint32_t v) {
  std::ostringstream s; s << std::uppercase << std::hex << std::setfill('0') << std::setw(8) << v; return s.str();
}
static std::string xex_version(uint32_t v) {
  std::ostringstream s;
  s << ((v>>28)&0xF) << "." << ((v>>24)&0xF) << "." << ((v>>8)&0xFFFF) << "." << (v&0xFF);
  return s.str();
}
static std::string utf16be_to_utf8(const uint8_t* p, size_t bytes) {
  std::wstring w;
  for (size_t i=0; i+1<bytes; i+=2) {
    uint16_t ch = uint16_t(p[i])<<8 | p[i+1];
    if (!ch) break;
    w.push_back(static_cast<wchar_t>(ch));
  }
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);
  std::string out(n,'\0');
  WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),out.data(),n,nullptr,nullptr);
  return out;
}
static std::string json_escape(const std::string& s) {
  std::ostringstream o;
  for (unsigned char c : s) {
    switch(c) {
      case '\\': o<<"\\\\"; break; case '"': o<<"\\\""; break;
      case '\n': o<<"\\n"; break; case '\r': o<<"\\r"; break; case '\t': o<<"\\t"; break;
      default: if (c<0x20) o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<(int)c<<std::dec; else o<<c;
    }
  }
  return o.str();
}
static std::string csv_escape(const std::string& s) {
  bool q=s.find_first_of(",\"\r\n")!=std::string::npos;
  if (!q) return s;
  std::string o="\"";
  for(char c:s){ if(c=='\"') o+="\"\""; else o+=c; }
  o+="\""; return o;
}
static const char* content_type_name(uint32_t t) {
  switch(t){
    case 0x1:return "SavedGame";
    case 0x2:return "MarketplaceContent";
    case 0x3:return "Publisher";
    case 0x1000:return "Xbox360Title";
    case 0x7000:return "GamesOnDemand";
    default:return "Unknown";
  }
}
struct Report {
  std::string file, magic, type_name, publisher, title_name, display_en, display_es;
  uint64_t size=0, declared_size=0;
  uint32_t type=0, metadata_version=0, media_id=0, title_id=0, ver=0, base_ver=0;
};
static std::string lang(const std::vector<uint8_t>& m, int lang_id, bool desc=false) {
  size_t base=0, idx=0;
  if(lang_id>=1 && lang_id<=9){ base=desc?0x9CD:0x0CD; idx=lang_id-1; }
  else if(lang_id>=10 && lang_id<=12){ base=desc?0x90D6:0x50D6; idx=lang_id-10; }
  else return {};
  return utf16be_to_utf8(m.data()+base+idx*256,256);
}
static bool inspect(const fs::path& path, Report& r, std::string& err) {
  std::ifstream f(path,std::ios::binary);
  if(!f){ err="cannot open file"; return false; }
  std::vector<uint8_t> h(kHeaderSize);
  f.read(reinterpret_cast<char*>(h.data()),h.size());
  if((size_t)f.gcount()<kHeaderSize){ err="file too small for STFS header"; return false; }
  std::string magic(reinterpret_cast<char*>(h.data()),4);
  if(magic!="LIVE" && magic!="PIRS" && magic!="CON "){ err="invalid STFS magic"; return false; }
  std::vector<uint8_t> m(h.begin()+kMetaOff,h.begin()+kMetaOff+kMetaSize);
  r.file=path.filename().u8string(); r.size=fs::file_size(path);
  r.magic=magic=="CON "?"CON":magic;
  r.type=be32(m,0); r.type_name=content_type_name(r.type);
  r.metadata_version=be32(m,4); r.declared_size=be64(m,8);
  r.media_id=be32(m,0x10); r.ver=be32(m,0x14); r.base_ver=be32(m,0x18); r.title_id=be32(m,0x1C);
  r.publisher=utf16be_to_utf8(m.data()+0x12CD,128);
  r.title_name=utf16be_to_utf8(m.data()+0x134D,128);
  r.display_en=lang(m,1); r.display_es=lang(m,5);
  return true;
}
static void write_json(const std::vector<Report>& rs, const fs::path& p) {
  std::ofstream o(p,std::ios::binary);
  o<<"[\n";
  for(size_t i=0;i<rs.size();++i){auto&r=rs[i];
    o<<"  {\n"
     <<"    \"file\": \""<<json_escape(r.file)<<"\",\n"
     <<"    \"size_bytes\": "<<r.size<<",\n"
     <<"    \"magic\": \""<<r.magic<<"\",\n"
     <<"    \"content_type\": \"0x"<<hex8(r.type)<<"\",\n"
     <<"    \"content_type_name\": \""<<r.type_name<<"\",\n"
     <<"    \"metadata_version\": "<<r.metadata_version<<",\n"
     <<"    \"declared_content_size\": "<<r.declared_size<<",\n"
     <<"    \"media_id\": \""<<hex8(r.media_id)<<"\",\n"
     <<"    \"version\": \""<<xex_version(r.ver)<<"\",\n"
     <<"    \"base_version\": \""<<xex_version(r.base_ver)<<"\",\n"
     <<"    \"title_id\": \""<<hex8(r.title_id)<<"\",\n"
     <<"    \"publisher\": \""<<json_escape(r.publisher)<<"\",\n"
     <<"    \"title_name\": \""<<json_escape(r.title_name)<<"\",\n"
     <<"    \"display_en\": \""<<json_escape(r.display_en)<<"\",\n"
     <<"    \"display_es\": \""<<json_escape(r.display_es)<<"\"\n"
     <<"  }"<<(i+1<rs.size()?",":"")<<"\n";
  }
  o<<"]\n";
}
static void write_csv(const std::vector<Report>& rs, const fs::path& p) {
  std::ofstream o(p,std::ios::binary);
  o<<"file,size_bytes,magic,content_type,content_type_name,metadata_version,declared_content_size,media_id,version,base_version,title_id,publisher,title_name,display_en,display_es\r\n";
  for(auto&r:rs){
    o<<csv_escape(r.file)<<","<<r.size<<","<<r.magic<<",0x"<<hex8(r.type)<<","<<r.type_name<<","
     <<r.metadata_version<<","<<r.declared_size<<","<<hex8(r.media_id)<<","<<xex_version(r.ver)<<","
     <<xex_version(r.base_ver)<<","<<hex8(r.title_id)<<","<<csv_escape(r.publisher)<<","
     <<csv_escape(r.title_name)<<","<<csv_escape(r.display_en)<<","<<csv_escape(r.display_es)<<"\r\n";
  }
}
int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);
  fs::path target = argc>1 ? fs::path(argv[1]) : fs::current_path()/L"Data"/L"DLC";
  std::vector<fs::path> files;
  if(fs::is_regular_file(target)) files.push_back(target);
  else if(fs::is_directory(target)) for(auto& e:fs::directory_iterator(target)) if(e.is_regular_file()) files.push_back(e.path());
  else {
    std::wcerr<<L"No existe la ruta: "<<target<<L"\n\nArrastra la carpeta de DLC sobre ASURA_DLC_Inspector.exe,\no ejecutalo junto a Data\\DLC.\n";
    if(argc<=1) system("pause"); return 2;
  }
  std::sort(files.begin(),files.end());
  std::vector<Report> rs;
  std::cout<<"ASURA'S WRATH - DLC STFS INSPECTOR (PORTABLE)\n";
  std::cout<<"================================================\n";
  for(auto&p:files){
    Report r; std::string err;
    if(inspect(p,r,err)){
      rs.push_back(r);
      std::cout<<"[OK] "<<r.file<<"\n     "<<(r.display_en.empty()?"(sin nombre)":r.display_en)
               <<"\n     TitleID="<<hex8(r.title_id)<<"  Type="<<r.type_name<<"\n";
    } else std::cout<<"[SKIP] "<<p.filename().u8string()<<": "<<err<<"\n";
  }
  fs::path base=fs::is_directory(target)?target:target.parent_path();
  auto jp=base/"DLC_REPORT.json", cp=base/"DLC_REPORT.csv";
  write_json(rs,jp); write_csv(rs,cp);
  std::cout<<"\nInformes creados:\n  "<<jp.u8string()<<"\n  "<<cp.u8string()<<"\n";
  if(argc<=1) system("pause");
  return rs.empty()?1:0;
}

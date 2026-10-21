// Independent P06c2 assertions: adoption002 + pinned R01--R18 source-derived
// fixtures. No product mapper, classifier or transition computes expectations.
#include <imagejxx/channel_display.hpp>
#include <imagejxx/channel_display_command.hpp>
#include <imagejxx/session.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace imagejxx;
namespace {
std::size_t checks=0;
std::vector<std::string> cases;
void named(const std::string &name){cases.push_back(name);}
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error((cases.empty()?"":cases.back()+": ")+message);}
template<class E=std::invalid_argument,class F>void rejects(F f,const char *message){bool caught=false;try{f();}catch(const E&){caught=true;}check(caught,message);}
MetadataObject object(const MetadataValue &v){return std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(v)->value);}
MetadataArray array(const MetadataValue &v){return std::get<MetadataArray>(std::get<std::shared_ptr<const MetadataTree>>(v)->value);}
std::uint64_t bits(double v){std::uint64_t b;std::memcpy(&b,&v,sizeof b);return b;}
double f32(std::uint32_t b){float v;std::memcpy(&v,&b,sizeof b);return static_cast<double>(v);}
std::vector<std::uint64_t> fingerprint(const Dataset &d){std::vector<std::uint64_t>b;auto s=d.shape();for(int t=0;t<s.t;++t)for(int c=0;c<s.c;++c)for(int z=0;z<s.z;++z)for(double v:d.plane({t,c,z})->pixels)b.push_back(bits(v));return b;}
Dataset formula(PixelType type=PixelType::UInt8,int channels=3){Stack planes;for(int t=0;t<2;++t)for(int c=0;c<channels;++c)for(int z=0;z<4;++z){Image p(5,3,1,type);for(int y=0;y<3;++y)for(int x=0;x<5;++x)p.at(x,y)=40*t+30*c+5*z+2*y+x;planes.push_back(std::move(p));}return Dataset::from_planes({2,channels,4,3,5,1},std::move(planes));}
Dataset columns(PixelType type,const std::vector<std::vector<double>>&values,FloatingSamplePolicy policy=FloatingSamplePolicy::FiniteOnly){Stack planes;for(const auto&row:values){Image p(static_cast<int>(row.size()),1,1,type);p.pixels=row;planes.push_back(std::move(p));}return Dataset::from_planes({1,static_cast<int>(values.size()),1,1,static_cast<int>(values[0].size()),1},std::move(planes),{},policy);}
ChannelDisplayState state(int count=3){ChannelDisplayState s;const std::array<std::string,3> names{"Red","Yellow","Cyan"};for(int c=0;c<count;++c){ChannelDisplayChannel ch;ch.lut.name=names[c%3];s.channels.push_back(ch);}return s;}
ChannelDisplayState projected(ChannelDisplayProjection p,int count=3){auto s=state(count);s.version=ChannelDisplayVersion::V2;s.projection=p;return s;}
std::array<std::uint8_t,3> row8(int r,int g,int b){return {static_cast<std::uint8_t>(r),static_cast<std::uint8_t>(g),static_cast<std::uint8_t>(b)};}
std::array<std::uint8_t,3> row8(const std::array<int,3>&a){return row8(a[0],a[1],a[2]);}
ChannelDisplayTable input_table(const std::string&name){ChannelDisplayTable a{};for(int i=0;i<256;++i){if(name=="red")a[i]=row8(i,0,0);else if(name=="inv_red")a[i]=row8(255-i,255,255);else if(name=="inv_yellow")a[i]=row8(255-i,255-i,255);else if(name=="inv_cyan")a[i]=row8(255,255-i,255-i);else if(name=="constant")a[i]=row8(37,91,143);else if(name=="custom_a")a[i]=row8((3*i+7)%256,(5*i+11)%256,(7*i+13)%256);else if(name=="custom_b")a[i]=row8((11*i+23)%256,(2*i+101)%256,(13*i+3)%256);else if(name=="custom_c")a[i]=row8((i*i+41)%256,(17*i+5)%256,255-i);else if(name=="last_step")a[i]=i==255?row8(250,240,230):row8(200-i/2,160-i/3,120-i/4);else throw std::invalid_argument("Unknown authored table");}return a;}
void custom(ChannelDisplayChannel&c,const ChannelDisplayTable&t){c.lut.name.clear();c.lut.table=t;}
ChannelDisplayTable from_hex(const std::string&s){check(s.size()==1536,"Frozen table literal length");ChannelDisplayTable a{};for(int i=0;i<256;++i)for(int k=0;k<3;++k)a[i][k]=std::stoi(s.substr(6*i+2*k,2),nullptr,16);return a;}
ChannelDisplayTable transformed_literal(const std::string&name){
  if (name=="red") return from_hex(
    "fffffffffefefffdfdfffcfcfffbfbfffafafff9f9fff8f8fff7f7fff6f6fff5f5fff4f4fff3f3fff2f2fff1f1fff0f0"
    "ffefefffeeeeffededffececffebebffeaeaffe9e9ffe8e8ffe7e7ffe6e6ffe5e5ffe4e4ffe3e3ffe2e2ffe1e1ffe0e0"
    "ffdfdfffdedeffddddffdcdcffdbdbffdadaffd9d9ffd8d8ffd7d7ffd6d6ffd5d5ffd4d4ffd3d3ffd2d2ffd1d1ffd0d0"
    "ffcfcfffceceffcdcdffccccffcbcbffcacaffc9c9ffc8c8ffc7c7ffc6c6ffc5c5ffc4c4ffc3c3ffc2c2ffc1c1ffc0c0"
    "ffbfbfffbebeffbdbdffbcbcffbbbbffbabaffb9b9ffb8b8ffb7b7ffb6b6ffb5b5ffb4b4ffb3b3ffb2b2ffb1b1ffb0b0"
    "ffafafffaeaeffadadffacacffababffaaaaffa9a9ffa8a8ffa7a7ffa6a6ffa5a5ffa4a4ffa3a3ffa2a2ffa1a1ffa0a0"
    "ff9f9fff9e9eff9d9dff9c9cff9b9bff9a9aff9999ff9898ff9797ff9696ff9595ff9494ff9393ff9292ff9191ff9090"
    "ff8f8fff8e8eff8d8dff8c8cff8b8bff8a8aff8989ff8888ff8787ff8686ff8585ff8484ff8383ff8282ff8181ff8080"
    "ff7f7fff7e7eff7d7dff7c7cff7b7bff7a7aff7979ff7878ff7777ff7676ff7575ff7474ff7373ff7272ff7171ff7070"
    "ff6f6fff6e6eff6d6dff6c6cff6b6bff6a6aff6969ff6868ff6767ff6666ff6565ff6464ff6363ff6262ff6161ff6060"
    "ff5f5fff5e5eff5d5dff5c5cff5b5bff5a5aff5959ff5858ff5757ff5656ff5555ff5454ff5353ff5252ff5151ff5050"
    "ff4f4fff4e4eff4d4dff4c4cff4b4bff4a4aff4949ff4848ff4747ff4646ff4545ff4444ff4343ff4242ff4141ff4040"
    "ff3f3fff3e3eff3d3dff3c3cff3b3bff3a3aff3939ff3838ff3737ff3636ff3535ff3434ff3333ff3232ff3131ff3030"
    "ff2f2fff2e2eff2d2dff2c2cff2b2bff2a2aff2929ff2828ff2727ff2626ff2525ff2424ff2323ff2222ff2121ff2020"
    "ff1f1fff1e1eff1d1dff1c1cff1b1bff1a1aff1919ff1818ff1717ff1616ff1515ff1414ff1313ff1212ff1111ff1010"
    "ff0f0fff0e0eff0d0dff0c0cff0b0bff0a0aff0909ff0808ff0707ff0606ff0505ff0404ff0303ff0202ff0101ff0000");
  if (name=="inv_red") return from_hex(
    "000000000101000202000303000404000505000606000707000808000909000a0a000b0b000c0c000d0d000e0e000f0f"
    "001010001111001212001313001414001515001616001717001818001919001a1a001b1b001c1c001d1d001e1e001f1f"
    "002020002121002222002323002424002525002626002727002828002929002a2a002b2b002c2c002d2d002e2e002f2f"
    "003030003131003232003333003434003535003636003737003838003939003a3a003b3b003c3c003d3d003e3e003f3f"
    "004040004141004242004343004444004545004646004747004848004949004a4a004b4b004c4c004d4d004e4e004f4f"
    "005050005151005252005353005454005555005656005757005858005959005a5a005b5b005c5c005d5d005e5e005f5f"
    "006060006161006262006363006464006565006666006767006868006969006a6a006b6b006c6c006d6d006e6e006f6f"
    "007070007171007272007373007474007575007676007777007878007979007a7a007b7b007c7c007d7d007e7e007f7f"
    "008080008181008282008383008484008585008686008787008888008989008a8a008b8b008c8c008d8d008e8e008f8f"
    "009090009191009292009393009494009595009696009797009898009999009a9a009b9b009c9c009d9d009e9e009f9f"
    "00a0a000a1a100a2a200a3a300a4a400a5a500a6a600a7a700a8a800a9a900aaaa00abab00acac00adad00aeae00afaf"
    "00b0b000b1b100b2b200b3b300b4b400b5b500b6b600b7b700b8b800b9b900baba00bbbb00bcbc00bdbd00bebe00bfbf"
    "00c0c000c1c100c2c200c3c300c4c400c5c500c6c600c7c700c8c800c9c900caca00cbcb00cccc00cdcd00cece00cfcf"
    "00d0d000d1d100d2d200d3d300d4d400d5d500d6d600d7d700d8d800d9d900dada00dbdb00dcdc00dddd00dede00dfdf"
    "00e0e000e1e100e2e200e3e300e4e400e5e500e6e600e7e700e8e800e9e900eaea00ebeb00ecec00eded00eeee00efef"
    "00f0f000f1f100f2f200f3f300f4f400f5f500f6f600f7f700f8f800f9f900fafa00fbfb00fcfc00fdfd00fefe00ffff");
  if (name=="inv_yellow") return from_hex(
    "00000000000100000200000300000400000500000600000700000800000900000a00000b00000c00000d00000e00000f"
    "00001000001100001200001300001400001500001600001700001800001900001a00001b00001c00001d00001e00001f"
    "00002000002100002200002300002400002500002600002700002800002900002a00002b00002c00002d00002e00002f"
    "00003000003100003200003300003400003500003600003700003800003900003a00003b00003c00003d00003e00003f"
    "00004000004100004200004300004400004500004600004700004800004900004a00004b00004c00004d00004e00004f"
    "00005000005100005200005300005400005500005600005700005800005900005a00005b00005c00005d00005e00005f"
    "00006000006100006200006300006400006500006600006700006800006900006a00006b00006c00006d00006e00006f"
    "00007000007100007200007300007400007500007600007700007800007900007a00007b00007c00007d00007e00007f"
    "00008000008100008200008300008400008500008600008700008800008900008a00008b00008c00008d00008e00008f"
    "00009000009100009200009300009400009500009600009700009800009900009a00009b00009c00009d00009e00009f"
    "0000a00000a10000a20000a30000a40000a50000a60000a70000a80000a90000aa0000ab0000ac0000ad0000ae0000af"
    "0000b00000b10000b20000b30000b40000b50000b60000b70000b80000b90000ba0000bb0000bc0000bd0000be0000bf"
    "0000c00000c10000c20000c30000c40000c50000c60000c70000c80000c90000ca0000cb0000cc0000cd0000ce0000cf"
    "0000d00000d10000d20000d30000d40000d50000d60000d70000d80000d90000da0000db0000dc0000dd0000de0000df"
    "0000e00000e10000e20000e30000e40000e50000e60000e70000e80000e90000ea0000eb0000ec0000ed0000ee0000ef"
    "0000f00000f10000f20000f30000f40000f50000f60000f70000f80000f90000fa0000fb0000fc0000fd0000fe0000ff");
  if (name=="inv_cyan") return from_hex(
    "0000000100000200000300000400000500000600000700000800000900000a00000b00000c00000d00000e00000f0000"
    "1000001100001200001300001400001500001600001700001800001900001a00001b00001c00001d00001e00001f0000"
    "2000002100002200002300002400002500002600002700002800002900002a00002b00002c00002d00002e00002f0000"
    "3000003100003200003300003400003500003600003700003800003900003a00003b00003c00003d00003e00003f0000"
    "4000004100004200004300004400004500004600004700004800004900004a00004b00004c00004d00004e00004f0000"
    "5000005100005200005300005400005500005600005700005800005900005a00005b00005c00005d00005e00005f0000"
    "6000006100006200006300006400006500006600006700006800006900006a00006b00006c00006d00006e00006f0000"
    "7000007100007200007300007400007500007600007700007800007900007a00007b00007c00007d00007e00007f0000"
    "8000008100008200008300008400008500008600008700008800008900008a00008b00008c00008d00008e00008f0000"
    "9000009100009200009300009400009500009600009700009800009900009a00009b00009c00009d00009e00009f0000"
    "a00000a10000a20000a30000a40000a50000a60000a70000a80000a90000aa0000ab0000ac0000ad0000ae0000af0000"
    "b00000b10000b20000b30000b40000b50000b60000b70000b80000b90000ba0000bb0000bc0000bd0000be0000bf0000"
    "c00000c10000c20000c30000c40000c50000c60000c70000c80000c90000ca0000cb0000cc0000cd0000ce0000cf0000"
    "d00000d10000d20000d30000d40000d50000d60000d70000d80000d90000da0000db0000dc0000dd0000de0000df0000"
    "e00000e10000e20000e30000e40000e50000e60000e70000e80000e90000ea0000eb0000ec0000ed0000ee0000ef0000"
    "f00000f10000f20000f30000f40000f50000f60000f70000f80000f90000fa0000fb0000fc0000fd0000fe0000ff0000");
  if (name=="constant") return from_hex(
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da"
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da"
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da"
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da"
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da"
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da"
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da"
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da"
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da"
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da"
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da"
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da"
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da"
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da"
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da"
    "70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da70a6da");
  if (name=="custom_a") return from_hex(
    "f2f6f8ebf1f5e4ecf2dde7efd6e2eccfdde9c8d8e6c1d3e3bacee0b3c9ddacc4daa5bfd79ebad497b5d190b0ce89abcb"
    "82a6c87ba1c5749cc26d97bf6692bc5f8db95888b65183b34a7eb04379ad3c74aa356fa72e6aa42765a120609e195b9b"
    "1256980b5195044c92b3fd45aaf640a1ef3b98e8368fe13186da2c7dd32774cc226bc51d62be1859b71350b00e47a909"
    "3ea204ff65c9fa62c8f55fc7f05cc6eb59c5e656c4e153c3dc50c2d74dc1d24ac0cd47bfc844bec341bdbe3ebcb739b9"
    "ae32b4a52baf9c24aa931da58a16a0810f9b7808966f0191fa8e20f3891dec841ae57f17de7a14d77511d0700ec96b0b"
    "c26608bb6105b45c0255ffa750fca64bf9a546f6a441f3a33cf0a237eda132eaa02de79f28e49e23e19d1ede9c19db9b"
    "14d89a0fd5990ad29805cf9700cc96612ffb5828f64f21f1461aec3d13e7340ce22b05ddfedab4f7d5b1f0d0aee9cbab"
    "e2c6a8dbc1a5d4bca2cdb79fc6b29cbfad99b8a896b1a393aa9e90a3998d9c948a958f878e8a8487858180807e797b7b"
    "7276786b7175646c725d676f56626c4f5d694858664153633a4e6033495d2c445a253f571e3a5417355110304e092b4b"
    "022648d5fb1fccf41ac3ed15bae610b1df0ba8d8069fd101fc3062f72d61f22a60ed275fe8245ee3215dde1e5cd91b5b"
    "d4185acf1559ca1258c50f57c00c56bb0955b60654b10353ac005253a9fd4ca4fa459ff73e9af43795f13090ee298beb"
    "2286e81b81e5147ce20d77df0672dc91ff6b88f8667ff16176ea5c6de35764dc525bd54d52ce4849c74340c03e39bb3b"
    "34b83a2fb5392ab23825af3720ac361ba93516a63411a3330ca032079d31029a30cb65fdc25ef8b957f3b050eea749e9"
    "9e42e4953bdf8c34da832dd57a26d0711fcb6818c65f11c1560abc4d03b7fcb46af5af67eeaa64e7a561e0a05ed99b5b"
    "d29658cb9155c48c52bd874fb6824caf7d49a87846a173439a6e4093693d8c643a855f377e5a3477553170502e694b2b"
    "6246285b4125543c224d371f46321c3f2d193828163123132a1e1023190d1c140a150f070e0a040705010000fef9fbfb");
  if (name=="custom_b") return from_hex(
    "aefc9aaaef98a6e296a2d5949ec8929abb9096ae8e92a18c8e948a888586867a86826d847e60827a538076467e72397c"
    "6e2c7a6a1f78661276620574f8920ceb7c016ef6866ceb866ae08668d58666ca8664bf8662b48660a9865e9e865c9386"
    "5a88864f747d425e7235486728325c1b1c51160e4e12014cf4da30e7c425daae1acd980fc0820440f9863eee863ce386"
    "3ad88638cd8636c28634b78632ac8630a1862e96862c8b86247a801764750a4e6ac2fd24bef022bae320b6d61eb2c91c"
    "aebc1aaaaf18a29e1295880712fc8610f1860ee6860cdb860ad08608c58606ba8604af8602a4860099867606fefc8386"
    "fa7886f86d86f66286f45786f24c86f04186ee3686ec2b86ea2086e81586e60a865d78ff5062f4434ce93636de3229dc"
    "2e1cda2a0fd82602d6f5c8a7e8b29cdb9c91ce8686cc7b86ca7086c86586c65a86c44f86c24486c03986be2e86bc2386"
    "ba1886b80d86b602862568f71852ec0b3ce1d6feaed2f1accee4aacad7a8c6caa6bdb89fb0a294a38c899e7e869c7386"
    "9a6886985d86965286944786923c869031868e26868c1b868a108688058686fa8684ef8682e48680d9867ece867cc386"
    "7ab88678ad8676a286749786728c866b7c815e667652516c4e446a4a3768462a66421d643e10623a0360f6b61ee9a013"
    "dc8a0858fd8656f28654e78652dc8650d1864ec6864cbb864ab08648a586469a86448f86408284336c7926566e194063"
    "0c2a58eaff38e6f236e2e534d8d22ccbbc21bea616b1900ba47a0028f58626ea8624df8622d48620c9861ebe861cb386"
    "1aa886189d8616928614878608727c9afb1096ee0e92e10c8ed40a8ac70886ba0682ad047ea0027a9300fe0e86fc0386"
    "6e6cfa6a5ff86652f66245f45e38f25a2bf0561eee5211ec4e04eaf7a495ea8e8ae47f86e27486e06986de5e86dc5386"
    "da4886d83d86d63286d42786d21c86d01186ce06864170fb345af02744e51a2eda0d18cf0002c4f3ecb9e6d6aed9c0a3"
    "ccaa98bf948db68286b47786b26c86b06186ae5686ac4b86aa4086a83586a62a86a41f86a21486a009861676fe0960f3");
  if (name=="custom_c") return from_hex(
    "2400fa1501e90802d80309cd0414c6051dbd0624b20729a5082c96092d850a2c720b295d0c24460d1d2d0c1210150ffb"
    "2410ea3511d94812c85d13b77414a68d15951684f21769c5184c96192d65281a401be9fd1cc4c60d8d7dc81efceb15e1"
    "2420da5521c98822b8a70d912454c62d258568267427a9e5286c96292d452aecf21f9d910e46283d2dfd882eecd52fdb"
    "303cd67531b9a812883349ad743486359ddd364472530521388c96392d2520b2980f3d11743cfedd3ded483edcb53fcb"
    "405cd69541a9427cd27d43874494c6653d5d46a4b2472925329680492d051072386d4bffee46e87d4dddcc1290954fbb"
    "507cd699357d525c9253c9ed664658559d9d44f2e045573322966011e59d285a00cd5bef745cdecd0d7dbc52b0755fab"
    "609cd689153d78526863898d56c6b845fddd66643225672312964001c55d6a6cf2df1d91ce46a8bd6dbd886eac6f89b5"
    "70bcd671edf5721c1273492d7474464d7535287624138521029620c579f1a87ae08d7bcf7c84c67d9dcd7eb4d27fc9d5"
    "7ad6d069d5bd58d2a847cd9136c67825bd5d14b24003a521e488f2e185ddd072b8bf5d91ae46689d2d3d8e14127bf5e1"
    "6ad6b059b57d4892485d93377494268d951504f260c597f3e296e0d1659dc032589be9fd9cc4c69d9d8d9e74529f4915"
    "5ad69055a14988a238a3890d16c6382da505f43280a7a9e5d296c0c1455daaecf2aba99dac64467dedbd88ae6caf8935"
    "4ad67075b139b29c121db31774b406cdb5f5e472a0d30521c296a0b92d25a8baa0bb693d7ec688bd9d4d5cd270b5bf4b"
    "3ad65095c12918d2287dc307f4c4f6e53d5dd4b2c0c72925c8ac96c92d05caac727fdd91cca4467dcd5dce941295cf3b"
    "2ad630b5d11948d208ddd3f7e64658d59d9dc4f2e0d76945d8cc9691e59dda8c32cddb6f74dc5edd9d0dc8de3c75df2b"
    "24e01ad5e109f85268e78d91e4d4c6c5fddde66432e7a965e4e892a5e98170f278eba91dece446bded3d88ee2c55ef1b"
    "24f00af5f1f9f21c12f3492df47446f59d5df6c472f7e985e4f882c5f971a8fa608dfb4f74fc3e5dfd2d48fe1c35ff0b");
  if (name=="last_step") return from_hex(
    "875f37875f37876038875f3888613988613988613a88613a89633b89623b89633c89633c8a643d8a643d8a653e8a643e"
    "8b663f8b663f8b66408b66408c68418c67418c68428c68428d69438d69438d6a448d69448e6b458e6b458e6b468e6b46"
    "8f6d478f6c478f6d488f6d48906e49906e49906f4a906e4a91704b91704b91704c91704c92724d92714d92724e92724e"
    "93734f93734f937450937350947551947551947552947552957753957653957754957754967855967855967956967856"
    "977a57977a57977a58977a58987c59987b59987c5a987c5a997d5b997d5b997e5c997d5c9a7f5d9a7f5d9a7f5e9a7f5e"
    "9b815f9b805f9b81609b81609c82619c82619c83629c82629d84639d84639d84649d84649e86659e85659e86669e8666"
    "9f87679f87679f88689f8768a08969a08969a0896aa0896aa18b6ba18a6ba18b6ca18b6ca28c6da28c6da28d6ea28c6e"
    "a38e6fa38e6fa38e70a38e70a49071a48f71a49072a49072a59173a59173a59274a59174a69375a69375a69376a69376"
    "a79577a79477a79578a79578a89679a89679a8977aa8967aa9987ba9987ba9987ca9987caa9a7daa997daa9a7eaa9a7e"
    "ab9b7fab9b7fab9c80ab9b80ac9d81ac9d81ac9d82ac9d82ad9f83ad9e83ad9f84ad9f84aea085aea085aea186aea086"
    "afa287afa287afa288afa288b0a489b0a389b0a48ab0a48ab1a58bb1a58bb1a68cb1a58cb2a78db2a78db2a78eb2a78e"
    "b3a98fb3a88fb3a990b3a990b4aa91b4aa91b4ab92b4aa92b5ac93b5ac93b5ac94b5ac94b6ae95b6ad95b6ae96b6ae96"
    "b7af97b7af97b7b098b7af98b8b199b8b199b8b19ab8b19ab9b39bb9b29bb9b39cb9b39cbab49dbab49dbab59ebab49e"
    "bbb69fbbb69fbbb6a0bbb6a0bcb8a1bcb7a1bcb8a2bcb8a2bdb9a3bdb9a3bdbaa4bdb9a4bebba5bebba5bebba6bebba6"
    "bfbda7bfbca7bfbda8bfbda8c0bea9c0bea9c0bfaac0beaac1c0abc1c0abc1c0acc1c0acc2c2adc2c1adc2c2aec2c2ae"
    "c3c3afc3c3afc2c3afc3c3b0c3c4b0c3c4b0c3c4b1c3c4b1c3c5b1c4c5b2c3c5b2c3c5b2c4c6b3c4c6b3c3c6b3190f05");
  throw std::invalid_argument("Unknown frozen transformed table");
}
void rgb(const Image&p,int w,int h,const std::vector<std::array<int,3>>&expected){check(p.width==w&&p.height==h&&p.channels==3&&p.type==PixelType::UInt8,"RGB geometry/type");check(expected.size()==static_cast<std::size_t>(w*h),"Literal pixel membership");for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int k=0;k<3;++k)check(p.at(x,y,k)==expected[y*w+x][k],"Independent RGB literal mismatch");}
MetadataObject wire(const std::string&mode="composite",bool v2=false,const std::string&projection="sum",int count=3){MetadataArray channels;const std::array<std::string,3>names{"Red","Yellow","Cyan"};for(int c=0;c<count;++c)channels.push_back(metadata_object({{"visible",true},{"range",metadata_array({0.0,255.0})},{"lut",metadata_object({{"name",names[c%3]}})}}));MetadataObject r{{"schema",std::string("imagejxx.channel-display")},{"version",std::int64_t(v2?2:1)},{"mode",mode},{"channels",metadata_array(channels)}};if(v2)r["projection"]=projection;return r;}
void position(Document&d,int t,int c,int z){auto v=d.snapshot().display();v.t=t;v.c=c;v.crosshair={2,1,z};d.set_display(std::move(v));}
void set_state(Document&d,const ChannelDisplayState&s){auto v=with_channel_display(d.snapshot().display(),d.snapshot().data().dataset,s);d.set_display(std::move(v));}
void state_cases(){
  const DatasetShape shape{2,3,4,3,5,1};const auto finite=FloatingSamplePolicy::FiniteOnly;
  named("S01.v1-wire-default-aggregate");
  ChannelDisplayState old{ChannelDisplayMode::Composite,state().channels};ChannelDisplayRequest old_request{ChannelDisplayAction::Set,old,{}};
  check(old.version==ChannelDisplayVersion::V1&&old.projection==ChannelDisplayProjection::Sum&&old_request.state->version==ChannelDisplayVersion::V1,"Old aggregate/default changed");
  check(metadata_equal(encode_channel_display(old,shape,PixelType::UInt8,finite),wire()),"V1 exact typed wire changed");
  Document literal(formula()),encoded(formula());auto lv=literal.snapshot().display();lv.properties["channel_display"]=metadata_object(wire());literal.set_display(lv);set_state(encoded,old);
  check(encode_session(SessionCapture{{literal.snapshot()},{},{},{}})==encode_session(SessionCapture{{encoded.snapshot()},{},{},{}}),"V1 session bytes differ from authored wire");
  for(auto p:{ChannelDisplayProjection::Max,ChannelDisplayProjection::Min}){old.projection=p;rejects([&]{encode_channel_display(old,shape,PixelType::UInt8,finite);},"V1 non-SUM encoded");rejects([&]{normalize_channel_display(old,shape,PixelType::UInt8,finite);},"V1 non-SUM normalized/upgraded");}
  named("S02.v2-nine-mode-projection-roundtrips");
  for(const auto&mode:std::array<std::string,3>{"composite","color","grayscale"})for(const auto&projection:std::array<std::string,3>{"sum","max","min"}){auto r=wire(mode,true,projection);auto s=parse_channel_display(r,shape,PixelType::UInt16,finite);check(metadata_equal(encode_channel_display(s,shape,PixelType::UInt16,finite),r),"V2 exact retained wire");check(s.version==ChannelDisplayVersion::V2,"V2 SUM downcast");}
  named("S03.v2-strict-fields-and-scalars");auto good=wire("composite",true,"min");
  for(const auto&key:{"schema","version","mode","projection","channels"}){auto r=good;r.erase(key);rejects([&]{parse_channel_display(r,shape,PixelType::UInt8,finite);},"Missing V2 field");}
  for(MetadataValue bad:{MetadataValue(true),MetadataValue(2.0),MetadataValue(std::int64_t(0)),MetadataValue(std::uint64_t(3))}){auto r=good;r["version"]=bad;rejects([&]{parse_channel_display(r,shape,PixelType::UInt8,finite);},"Bad V2 version");}
  for(MetadataValue bad:{MetadataValue(true),MetadataValue(std::string("MAX")),MetadataValue(std::string("invert")),MetadataValue(std::monostate{})}){auto r=good;r["projection"]=bad;rejects([&]{parse_channel_display(r,shape,PixelType::UInt8,finite);},"Bad projection");}
  auto extra=good;extra["extra"]=1;rejects([&]{parse_channel_display(extra,shape,PixelType::UInt8,finite);},"Unknown V2 field");auto mixed=wire();mixed["projection"]=std::string("sum");rejects([&]{parse_channel_display(mixed,shape,PixelType::UInt8,finite);},"V1 projection extension");
  auto unsigned_version=good;unsigned_version["version"]=std::uint64_t(2);check(parse_channel_display(unsigned_version,shape,PixelType::UInt8,finite).projection==ChannelDisplayProjection::Min,"Exact unsigned V2 refused");
  auto typed=projected(ChannelDisplayProjection::Min);typed.projection=static_cast<ChannelDisplayProjection>(99);rejects([&]{validate_channel_display(typed,shape,PixelType::UInt8,finite);},"Invalid typed projection");typed=projected(ChannelDisplayProjection::Min);typed.version=static_cast<ChannelDisplayVersion>(99);rejects([&]{validate_channel_display(typed,shape,PixelType::UInt8,finite);},"Invalid typed version");
  named("S04.retained-reset-mode-and-c9-owned-normalization");
  for(auto projection:{ChannelDisplayProjection::Sum,ChannelDisplayProjection::Max,ChannelDisplayProjection::Min}){auto s=projected(projection);auto reset=reset_channel_display_ranges(formula(PixelType::UInt16),s,1,2,{2});check(reset.first.version==s.version&&reset.first.projection==projection,"Reset lost V2 identity/projection");check(reset.first.channels[2].range==std::array<double,2>{110,118},"Reset no longer live captured XY");s.mode=ChannelDisplayMode::Color;check(normalize_channel_display(s,shape,PixelType::UInt8,finite).projection==projection,"Mode edit lost projection");auto many=projected(projection,9);auto normalized=normalize_channel_display(many,{2,9,4,3,5,1},PixelType::UInt8,finite);check(normalized.channels.size()==9&&normalized.mode==ChannelDisplayMode::Color&&normalized.version==ChannelDisplayVersion::V2&&normalized.projection==projection,"C9 normalization truncated/downcast");rejects([&]{parse_channel_display(wire("composite",true,projection==ChannelDisplayProjection::Sum?"sum":projection==ChannelDisplayProjection::Max?"max":"min",9),{2,9,4,3,5,1},PixelType::UInt8,finite);},"Stored C9 Composite admitted");}
  named("S05.native-v3-v2sum-min-and-future-transport");
  for(auto projection:{ChannelDisplayProjection::Sum,ChannelDisplayProjection::Max,ChannelDisplayProjection::Min}){Document d(formula());auto s=projected(projection);set_state(d,s);auto capture=SessionCapture{{d.snapshot()},{},{},{}};auto restored=decode_session(encode_session(capture));check(restored.documents.size()==1,"Native V3 document membership");check(metadata_equal(restored.documents[0]->snapshot().display().properties,d.snapshot().display().properties),"Native V3 rewrote V2 state");check(fingerprint(restored.documents[0]->snapshot().data().dataset)==fingerprint(d.snapshot().data().dataset),"Native V3 changed source");}
  Document future(formula());auto v=future.snapshot().display();auto opaque=metadata_object({{"schema",std::string("imagejxx.channel-display")},{"version",std::int64_t(4)},{"unknown",metadata_array({true,7,std::string("retained")})}});v.properties["channel_display"]=opaque;future.set_display(v);auto restored=decode_session(encode_session(SessionCapture{{future.snapshot()},{},{},{}}));check(metadata_equal(restored.documents[0]->snapshot().display().properties.at("channel_display"),opaque),"Future state lost in native transport");rejects([&]{parse_channel_display(object(opaque),shape,PixelType::UInt8,finite);},"Future interpreted");
  named("S06.v2-dtype-shape-and-c-count-admission");auto s=projected(ChannelDisplayProjection::Min);rejects([&]{validate_channel_display(s,shape,PixelType::Float64,finite);},"Float64 projection admitted");rejects([&]{validate_channel_display(s,{2,3,4,3,5,3},PixelType::UInt8,finite);},"Interleaved projection admitted");rejects([&]{validate_channel_display(s,shape,PixelType::UInt8,FloatingSamplePolicy::PreserveQuietNaNFloat32);},"Integer quiet policy admitted");Document d(formula());set_state(d,s);auto before=d.snapshot();auto edit=d.begin_edit();edit.replace_dataset(formula(PixelType::UInt8,2));rejects([&]{d.commit(edit);},"V2 C-count changed without remap");check(edit.active()&&d.snapshot().shares_data(before),"C-count refusal not atomic");edit.rollback();
}
std::vector<std::array<int,3>> mapped_literals(Plane plane,ChannelDisplayProjection projection){
  if (plane==Plane::XY && projection==ChannelDisplayProjection::Max) return {{180,150,210}, {179,149,209}, {178,148,208}, {177,147,207}, {176,146,206}, {178,148,208}, {177,147,207}, {176,146,206}, {175,145,205}, {174,144,204}, {176,146,206}, {175,145,205}, {174,144,204}, {173,143,203}, {172,142,202}};
  if (plane==Plane::XY && projection==ChannelDisplayProjection::Min) return {{45,22,37}, {46,23,38}, {47,23,38}, {48,24,39}, {49,24,39}, {47,23,38}, {48,24,39}, {49,24,39}, {50,25,40}, {51,25,40}, {49,24,39}, {50,25,40}, {51,25,40}, {52,26,41}, {53,26,41}};
  if (plane==Plane::XZ && projection==ChannelDisplayProjection::Max) return {{183,153,213}, {182,152,212}, {181,151,211}, {180,150,210}, {179,149,209}, {178,148,208}, {177,147,207}, {176,146,206}, {175,145,205}, {174,144,204}, {173,143,203}, {172,142,202}, {171,141,201}, {170,140,200}, {169,139,199}, {168,138,198}, {167,137,197}, {166,136,196}, {165,135,195}, {164,134,194}};
  if (plane==Plane::XZ && projection==ChannelDisplayProjection::Min) return {{42,21,36}, {43,21,36}, {44,22,37}, {45,22,37}, {46,23,38}, {47,23,38}, {48,24,39}, {49,24,39}, {50,25,40}, {51,25,40}, {52,26,41}, {53,26,41}, {54,27,42}, {55,27,42}, {56,28,43}, {57,28,43}, {58,29,44}, {59,29,44}, {60,30,45}, {60,30,45}};
  if (plane==Plane::YZ && projection==ChannelDisplayProjection::Max) return {{184,154,214}, {182,152,212}, {180,150,210}, {179,149,209}, {177,147,207}, {175,145,205}, {174,144,204}, {172,142,202}, {170,140,200}, {169,139,199}, {167,137,197}, {165,135,195}};
  if (plane==Plane::YZ && projection==ChannelDisplayProjection::Min) return {{41,20,35}, {43,21,36}, {45,22,37}, {46,23,38}, {48,24,39}, {50,25,40}, {51,25,40}, {53,26,41}, {55,27,42}, {56,28,43}, {58,29,44}, {60,30,45}};
 throw std::invalid_argument("Unknown mapping fixture");
}
void mapping_cases(){
  const std::array<PixelType,3>types{PixelType::UInt8,PixelType::UInt16,PixelType::Float32};const std::array<Plane,3>planes{Plane::XY,Plane::XZ,Plane::YZ};int row=0;
  for(auto type:types)for(auto plane:planes)for(auto projection:{ChannelDisplayProjection::Max,ChannelDisplayProjection::Min}){named("M"+std::to_string(++row)+".dtype-plane-projection");auto d=formula(type);auto before=fingerprint(d);auto s=projected(projection);for(int c=0;c<3;++c){ChannelDisplayTable a{};for(int i=0;i<256;++i)a[i]=c==0?row8(i,i/2,255-i):c==1?row8(255-i,i,i/2):row8(i/2,255-i,i);custom(s.channels[c],a);}auto encoded=encode_channel_display(s,d.shape(),d.pixel_type(),d.sample_policy());auto out=compose_channel_display(d,s,plane,1,1,2);rgb(out,plane==Plane::YZ?3:5,plane==Plane::XY?3:4,mapped_literals(plane,projection));check(fingerprint(d)==before&&metadata_equal(encoded,encode_channel_display(s,d.shape(),d.pixel_type(),d.sample_policy())),"Mapper mutated source/state");}
  named("E01.overlap-sum-max-min");auto d=columns(PixelType::UInt8,{{64},{128},{192}});auto s=projected(ChannelDisplayProjection::Sum);const std::array<std::array<int,3>,3>colors{{{200,30,90},{80,150,60},{120,70,220}}};for(int c=0;c<3;++c){ChannelDisplayTable a{};a.fill(row8(colors[c]));custom(s.channels[c],a);}for(auto p:{ChannelDisplayProjection::Sum,ChannelDisplayProjection::Max,ChannelDisplayProjection::Min}){s.projection=p;rgb(compose_channel_display(d,s,Plane::XY,0,0,0),1,1,{p==ChannelDisplayProjection::Sum?std::array<int,3>{255,250,255}:p==ChannelDisplayProjection::Max?std::array<int,3>{200,150,220}:std::array<int,3>{80,30,60}});}
  named("E02.empty-fresh-warm-first-pixel");for(auto p:{ChannelDisplayProjection::Max,ChannelDisplayProjection::Min}){s.projection=p;(void)compose_channel_display(d,s,Plane::XY,0,0,0);for(auto&ch:s.channels)ch.visible=false;int reads=0;auto never=[&](Plane,int,int,int)->std::shared_ptr<const Image>{++reads;throw std::runtime_error("Empty provider read");};for(int repeat=0;repeat<2;++repeat)rgb(compose_channel_display(d,s,Plane::XY,0,0,0,never),1,1,{{0,0,0}});check(reads==0,"Empty composition called provider");for(auto&ch:s.channels)ch.visible=true;}
  named("E03.first-hidden-one-positive-contributor");s.channels[0].visible=false;s.channels[2].visible=false;for(auto p:{ChannelDisplayProjection::Max,ChannelDisplayProjection::Min}){s.projection=p;rgb(compose_channel_display(d,s,Plane::XY,0,0,0),1,1,{{80,150,60}});}
  named("E04.singleton-gray-rule");auto one=columns(PixelType::UInt8,{{10,20,30}});for(auto p:{ChannelDisplayProjection::Max,ChannelDisplayProjection::Min}){auto only=projected(p,1);custom(only.channels[0],input_table("constant"));rgb(compose_channel_display(one,only,Plane::XY,0,0,0),3,1,{{10,10,10},{20,20,20},{30,30,30}});}
  named("E05.c8-and-owned-c9-fallback");std::vector<std::vector<double>>values(9,std::vector<double>{17});for(int count:{8,9}){auto many=columns(PixelType::UInt8,std::vector<std::vector<double>>(values.begin(),values.begin()+count));for(auto p:{ChannelDisplayProjection::Max,ChannelDisplayProjection::Min}){auto ms=projected(p,count);for(int c=0;c<count;++c){ChannelDisplayTable a{};a.fill(row8(10*c+1,10*c+2,10*c+3));custom(ms.channels[c],a);}ms=normalize_channel_display(ms,many.shape(),many.pixel_type(),many.sample_policy());rgb(compose_channel_display(many,ms,Plane::XY,0,0,count-1),1,1,{count==9?std::array<int,3>{81,82,83}:p==ChannelDisplayProjection::Max?std::array<int,3>{71,72,73}:std::array<int,3>{1,2,3}});}}
  named("E06.quiet-nan-custom-zero-and-negative-zero");auto quiet=columns(PixelType::Float32,{{f32(0x7fc12345),-0.0,1},{.5,.25,.75}},FloatingSamplePolicy::PreserveQuietNaNFloat32);auto qb=fingerprint(quiet);auto qs=projected(ChannelDisplayProjection::Max,2);for(auto&ch:qs.channels)ch.range={0,1};auto a=input_table("red");for(int i=0;i<256;++i)a[i]=row8(i,i,255-i);a[0]={17,31,47};custom(qs.channels[0],a);a.fill({10,40,20});custom(qs.channels[1],a);rgb(compose_channel_display(quiet,qs,Plane::XY,0,0,0),3,1,{{17,40,47},{17,40,47},{255,255,20}});qs.projection=ChannelDisplayProjection::Min;rgb(compose_channel_display(quiet,qs,Plane::XY,0,0,0),3,1,{{10,31,20},{10,31,20},{10,40,0}});check(fingerprint(quiet)==qb,"Quiet payload/sign/negative zero mutated");
  named("E07.inactive-projection-color-gray");auto two=columns(PixelType::UInt8,{{50},{100}});for(auto mode:{ChannelDisplayMode::Color,ChannelDisplayMode::Grayscale})for(auto p:{ChannelDisplayProjection::Max,ChannelDisplayProjection::Min}){auto cs=projected(p,2);cs.mode=mode;cs.channels[1].lut.name="Green";for(auto&ch:cs.channels)ch.visible=false;rgb(compose_channel_display(two,cs,Plane::XY,0,0,1),1,1,{mode==ChannelDisplayMode::Color?std::array<int,3>{0,100,0}:std::array<int,3>{100,100,100}});check(cs.version==ChannelDisplayVersion::V2&&cs.projection==p,"Inactive render rewrote V2");}
  named("E08.provider-callback-captures-complete-input");auto captured=projected(ChannelDisplayProjection::Max);for(int c=0;c<3;++c){ChannelDisplayTable table{};table.fill(row8(colors[c]));custom(captured.channels[c],table);}int calls=0;auto provider=[&](Plane,int,int,int c){++calls;captured.projection=ChannelDisplayProjection::Sum;for(auto&ch:captured.channels){ch.visible=false;ch.range={0,0};}return d.plane({0,c,0});};rgb(compose_channel_display(d,captured,Plane::XY,0,0,0,provider),1,1,{{200,150,220}});check(calls==3,"Captured contributor list changed inside callback");
}
void transition_cases(){
  for(int r=12;r<=18;++r){named("T"+std::to_string(r)+".qualified-host-transition");Document d(formula());position(d,1,r==15?2:1,2);auto s=state();s.channels[0].visible=true;s.channels[1].visible=false;s.channels[2].visible=true;for(int c=0;c<3;++c)s.channels[c].range=c==0?std::array<double,2>{1,201}:c==1?std::array<double,2>{7,219}:std::array<double,2>{11,241};std::array<std::string,3>names{"","",""};if(r==13){s.mode=ChannelDisplayMode::Color;s.version=ChannelDisplayVersion::V2;s.projection=ChannelDisplayProjection::Max;s.channels[0].visible=false;s.channels[1].visible=true;s.channels[2].visible=false;}if(r==14||r==15){names={"inv_red","inv_yellow","inv_cyan"};if(r==14){s.mode=ChannelDisplayMode::Grayscale;s.version=ChannelDisplayVersion::V2;s.projection=ChannelDisplayProjection::Max;}}if(r>=16)names={"red",r==16?"inv_yellow":r==17?"constant":"last_step",r==16?"custom_c":r==17?"custom_b":"custom_a"};for(int c=0;c<3;++c)if(!names[c].empty())custom(s.channels[c],input_table(names[c]));set_state(d,s);auto before=d.snapshot();auto raw=fingerprint(before.data().dataset);auto original=encode_channel_display(s,d.snapshot().data().dataset.shape(),PixelType::UInt8,FloatingSamplePolicy::FiniteOnly);auto command=resolve_channel_display_command(before,{ChannelDisplayAction::SelectMax,{},{}});check(command.version()==ChannelDisplayVersion::V2&&command.state()->version==ChannelDisplayVersion::V2,"SelectMax V2 identity");check(command.state()->mode==ChannelDisplayMode::Composite,"SelectMax mode");check(command.state()->projection==(r>=16?ChannelDisplayProjection::Min:ChannelDisplayProjection::Max),"Faithful Max/Min result");check(command.navigation_channel()==(r>=15?std::optional<int>(0):std::nullopt),"Deliberate C effect");check(command.display_dependency_revision()==before.revisions().display&&command.requires_captured_display(),"Captured display dependency");for(int c=0;c<3;++c){const auto&out=command.state()->channels[c];check(out.visible==((r==13||r==14)?true:s.channels[c].visible),"Mode entry flags");check(out.range==s.channels[c].range,"Transition changed requested range");if(r>=15){check(out.lut.name.empty()&&out.lut.table.has_value(),"Transformed table not explicit custom");check(*out.lut.table==transformed_literal(names[c]),"Full transformed LUT differs from frozen source literal");}else{auto outstate=*command.state();outstate.version=s.version;outstate.projection=s.projection;outstate.mode=s.mode;for(int k=0;k<3;++k)outstate.channels[k].visible=s.channels[k].visible;check(metadata_equal(encode_channel_display(outstate,before.data().dataset.shape(),PixelType::UInt8,FloatingSamplePolicy::FiniteOnly),original),"Nontransform changed LUT/range state");}}
    auto result=command_display_state(command,before);check(result.t==1&&result.c==(r>=15?0:(r==15?2:1))&&result.crosshair[2]==2,"Transition position effect");auto hist=d.history();d.set_display(result);check(d.snapshot().shares_data(before)&&fingerprint(d.snapshot().data().dataset)==raw,"Transition mutated source pixels/data");check(d.snapshot().revisions().pixels==before.revisions().pixels&&d.snapshot().revisions().metadata==before.revisions().metadata&&d.snapshot().dirty()==before.dirty()&&d.history().undo_steps==hist.undo_steps&&d.history().redo_steps==hist.redo_steps,"Transition changed data/history/dirty");
    if(r==18){named("T18.repeat-from-min-to-max");auto again=resolve_channel_display_command(d.snapshot(),{ChannelDisplayAction::SelectMax,{},{}});check(again.state()->projection==ChannelDisplayProjection::Max&&again.navigation_channel()==std::optional<int>(0),"R18 repeated Max effect");for(int c=0;c<3;++c)check(again.state()->channels[c].lut.table&&*again.state()->channels[c].lut.table==input_table(names[c]),"Repeated transform did not restore full original table bytes");check(command_display_state(again,d.snapshot()).c==0,"Repeated same-C effect lost");}
  }
  named("T19.classifier-254-versus-255-boundary");for(int rising:{254,255}){Document d(formula());position(d,1,1,2);auto s=state();ChannelDisplayTable table{};table.fill({100,100,100});table[rising]={101,101,101};custom(s.channels[1],table);set_state(d,s);auto c=resolve_channel_display_command(d.snapshot(),{ChannelDisplayAction::SelectMax,{},{}});check(c.navigation_channel()==(rising==255?std::optional<int>(0):std::nullopt),"Classifier tested wrong final adjacent step");check(c.state()->projection==(rising==255?ChannelDisplayProjection::Min:ChannelDisplayProjection::Max),"Classifier boundary projection");}
}
} // namespace
int main(int argc,char**argv){try{if(argc>2)throw std::invalid_argument("Usage: projection [state|mapping|transition|all]");std::string group=argc==2?argv[1]:"all";if(group!="all"&&group!="state"&&group!="mapping"&&group!="transition")throw std::invalid_argument("Unknown projection group");if(group=="all"||group=="state")state_cases();if(group=="all"||group=="mapping")mapping_cases();if(group=="all"||group=="transition")transition_cases();const std::size_t expected_cases=group=="all"?41:group=="state"?6:group=="mapping"?26:9;check(cases.size()==expected_cases,"Frozen named membership changed");auto sorted=cases;std::sort(sorted.begin(),sorted.end());check(std::adjacent_find(sorted.begin(),sorted.end())==sorted.end(),"Duplicate named case");std::cout<<"channel_display_projection "<<group<<": "<<checks<<" checks; "<<cases.size()<<" named cases\n";for(const auto&name:cases)std::cout<<name<<'\n';return 0;}catch(const std::exception&e){std::cerr<<"channel_display_projection after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}

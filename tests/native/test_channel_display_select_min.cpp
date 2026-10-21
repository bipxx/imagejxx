// Independent P06c3 SelectMin assertions, adoption P06c3.SelectMin.V01.001.
// N07 tests the declared native logical-state policy (Min/Min), not Java's
// processor-cache Max/Max behavior. No product transition computes a golden.
#include <imagejxx/channel_display_command.hpp>
#include <imagejxx/session.hpp>
#include <algorithm>
#include <array>
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
std::size_t checks = 0;
std::vector<std::string> cases;
void named(const std::string &name) { cases.push_back(name); }
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error((cases.empty() ? "" : cases.back() + ": ") + message);
}
template<class E = std::invalid_argument, class F>
void rejects(F f, const char *message) {
  bool caught = false;
  try { f(); } catch (const E &) { caught = true; }
  check(caught, message);
}
MetadataObject object(const MetadataValue &v) {
  return std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(v)->value);
}
MetadataArray array(const MetadataValue &v) {
  return std::get<MetadataArray>(std::get<std::shared_ptr<const MetadataTree>>(v)->value);
}
MetadataObject changed(MetadataObject record, const std::string &field,
                       const std::function<void(MetadataObject &)> &edit) {
  auto nested = object(record.at(field)); edit(nested);
  record[field] = metadata_object(std::move(nested)); return record;
}
std::uint64_t bits(double value) {
  std::uint64_t result; std::memcpy(&result, &value, sizeof result); return result;
}
std::vector<std::uint64_t> fingerprint(const Dataset &data) {
  std::vector<std::uint64_t> result; auto shape = data.shape();
  for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z)
    for (double value : data.plane({t,c,z})->pixels) result.push_back(bits(value));
  return result;
}
Dataset formula(PixelType type = PixelType::UInt8, int channels = 3, int offset = 0, int width = 5) {
  Stack planes;
  for (int t=0;t<2;++t) for (int c=0;c<channels;++c) for (int z=0;z<4;++z) {
    Image p(width,3,1,type);
    for (int y=0;y<3;++y) for (int x=0;x<width;++x) p.at(x,y)=offset+40*t+30*c+5*z+2*y+x;
    planes.push_back(std::move(p));
  }
  SpatialCalibration cal; cal.spacing={.5,1.25,2.5}; cal.origin={11,-7,19};
  cal.unit="um"; cal.frame_interval=.75; cal.time_origin=2.25;
  return Dataset::from_planes({2,channels,4,3,width,1},std::move(planes),cal);
}
std::array<std::uint8_t,3> row(int r,int g,int b) {
  return {static_cast<std::uint8_t>(r),static_cast<std::uint8_t>(g),static_cast<std::uint8_t>(b)};
}
// Input formulas are independently spelled frozen N01--N08 fixture data.
ChannelDisplayTable input_table(const std::string &name) {
  ChannelDisplayTable result{};
  for (int i=0;i<256;++i) {
    if (name=="red") result[i]=row(i,0,0);
    else if (name=="yellow") result[i]=row(i,i,0);
    else if (name=="cyan") result[i]=row(0,i,i);
    else if (name=="inv_red") result[i]=row(255-i,255,255);
    else if (name=="inv_yellow") result[i]=row(255-i,255-i,255);
    else if (name=="inv_cyan") result[i]=row(255,255-i,255-i);
    else if (name=="constant") result[i]=row(37,91,143);
    else if (name=="custom_a") result[i]=row((3*i+7)%256,(5*i+11)%256,(7*i+13)%256);
    else if (name=="custom_b") result[i]=row((11*i+23)%256,(2*i+101)%256,(13*i+3)%256);
    else if (name=="custom_c") result[i]=row((i*i+41)%256,(17*i+5)%256,255-i);
    else if (name=="last_step") result[i]=i==255?row(250,240,230):row(200-i/2,160-i/3,120-i/4);
    else throw std::invalid_argument("Unknown authored input table");
  }
  return result;
}
ChannelDisplayTable from_hex(const std::string &hex) {
  check(hex.size()==1536,"Frozen complete LUT literal length");
  ChannelDisplayTable result{};
  for (int i=0;i<256;++i) for (int k=0;k<3;++k)
    result[i][k]=static_cast<std::uint8_t>(std::stoi(hex.substr(6*i+2*k,2),nullptr,16));
  return result;
}
// Full byte literals, frozen from the pinned source arithmetic independently
// of this product. The previously qualified Max macro has identical LUT data.
ChannelDisplayTable transformed_literal(const std::string &name) {
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
  if (name=="yellow") return from_hex(
    "fffffffffffefffffdfffffcfffffbfffffafffff9fffff8fffff7fffff6fffff5fffff4fffff3fffff2fffff1fffff0"
    "ffffefffffeeffffedffffecffffebffffeaffffe9ffffe8ffffe7ffffe6ffffe5ffffe4ffffe3ffffe2ffffe1ffffe0"
    "ffffdfffffdeffffddffffdcffffdbffffdaffffd9ffffd8ffffd7ffffd6ffffd5ffffd4ffffd3ffffd2ffffd1ffffd0"
    "ffffcfffffceffffcdffffccffffcbffffcaffffc9ffffc8ffffc7ffffc6ffffc5ffffc4ffffc3ffffc2ffffc1ffffc0"
    "ffffbfffffbeffffbdffffbcffffbbffffbaffffb9ffffb8ffffb7ffffb6ffffb5ffffb4ffffb3ffffb2ffffb1ffffb0"
    "ffffafffffaeffffadffffacffffabffffaaffffa9ffffa8ffffa7ffffa6ffffa5ffffa4ffffa3ffffa2ffffa1ffffa0"
    "ffff9fffff9effff9dffff9cffff9bffff9affff99ffff98ffff97ffff96ffff95ffff94ffff93ffff92ffff91ffff90"
    "ffff8fffff8effff8dffff8cffff8bffff8affff89ffff88ffff87ffff86ffff85ffff84ffff83ffff82ffff81ffff80"
    "ffff7fffff7effff7dffff7cffff7bffff7affff79ffff78ffff77ffff76ffff75ffff74ffff73ffff72ffff71ffff70"
    "ffff6fffff6effff6dffff6cffff6bffff6affff69ffff68ffff67ffff66ffff65ffff64ffff63ffff62ffff61ffff60"
    "ffff5fffff5effff5dffff5cffff5bffff5affff59ffff58ffff57ffff56ffff55ffff54ffff53ffff52ffff51ffff50"
    "ffff4fffff4effff4dffff4cffff4bffff4affff49ffff48ffff47ffff46ffff45ffff44ffff43ffff42ffff41ffff40"
    "ffff3fffff3effff3dffff3cffff3bffff3affff39ffff38ffff37ffff36ffff35ffff34ffff33ffff32ffff31ffff30"
    "ffff2fffff2effff2dffff2cffff2bffff2affff29ffff28ffff27ffff26ffff25ffff24ffff23ffff22ffff21ffff20"
    "ffff1fffff1effff1dffff1cffff1bffff1affff19ffff18ffff17ffff16ffff15ffff14ffff13ffff12ffff11ffff10"
    "ffff0fffff0effff0dffff0cffff0bffff0affff09ffff08ffff07ffff06ffff05ffff04ffff03ffff02ffff01ffff00");
  if (name=="cyan") return from_hex(
    "fffffffefffffdfffffcfffffbfffffafffff9fffff8fffff7fffff6fffff5fffff4fffff3fffff2fffff1fffff0ffff"
    "efffffeeffffedffffecffffebffffeaffffe9ffffe8ffffe7ffffe6ffffe5ffffe4ffffe3ffffe2ffffe1ffffe0ffff"
    "dfffffdeffffddffffdcffffdbffffdaffffd9ffffd8ffffd7ffffd6ffffd5ffffd4ffffd3ffffd2ffffd1ffffd0ffff"
    "cfffffceffffcdffffccffffcbffffcaffffc9ffffc8ffffc7ffffc6ffffc5ffffc4ffffc3ffffc2ffffc1ffffc0ffff"
    "bfffffbeffffbdffffbcffffbbffffbaffffb9ffffb8ffffb7ffffb6ffffb5ffffb4ffffb3ffffb2ffffb1ffffb0ffff"
    "afffffaeffffadffffacffffabffffaaffffa9ffffa8ffffa7ffffa6ffffa5ffffa4ffffa3ffffa2ffffa1ffffa0ffff"
    "9fffff9effff9dffff9cffff9bffff9affff99ffff98ffff97ffff96ffff95ffff94ffff93ffff92ffff91ffff90ffff"
    "8fffff8effff8dffff8cffff8bffff8affff89ffff88ffff87ffff86ffff85ffff84ffff83ffff82ffff81ffff80ffff"
    "7fffff7effff7dffff7cffff7bffff7affff79ffff78ffff77ffff76ffff75ffff74ffff73ffff72ffff71ffff70ffff"
    "6fffff6effff6dffff6cffff6bffff6affff69ffff68ffff67ffff66ffff65ffff64ffff63ffff62ffff61ffff60ffff"
    "5fffff5effff5dffff5cffff5bffff5affff59ffff58ffff57ffff56ffff55ffff54ffff53ffff52ffff51ffff50ffff"
    "4fffff4effff4dffff4cffff4bffff4affff49ffff48ffff47ffff46ffff45ffff44ffff43ffff42ffff41ffff40ffff"
    "3fffff3effff3dffff3cffff3bffff3affff39ffff38ffff37ffff36ffff35ffff34ffff33ffff32ffff31ffff30ffff"
    "2fffff2effff2dffff2cffff2bffff2affff29ffff28ffff27ffff26ffff25ffff24ffff23ffff22ffff21ffff20ffff"
    "1fffff1effff1dffff1cffff1bffff1affff19ffff18ffff17ffff16ffff15ffff14ffff13ffff12ffff11ffff10ffff"
    "0fffff0effff0dffff0cffff0bffff0affff09ffff08ffff07ffff06ffff05ffff04ffff03ffff02ffff01ffff00ffff");
  throw std::invalid_argument("Unknown frozen output table");
}
ChannelDisplayState fixture(int n) {
  const std::array<std::array<std::string,3>,8> names{{
    {{"red","yellow","cyan"}}, {{"inv_red","inv_yellow","inv_cyan"}},
    {{"inv_red","yellow","custom_c"}}, {{"red","constant","custom_b"}},
    {{"red","last_step","custom_a"}}, {{"red","yellow","cyan"}},
    {{"red","inv_yellow","custom_c"}}, {{"red","inv_yellow","custom_c"}}
  }};
  ChannelDisplayState s;
  if (n==2||n==3||n>=6) { s.version=ChannelDisplayVersion::V2; s.projection=ChannelDisplayProjection::Max; }
  if (n==6||n==8) s.mode=ChannelDisplayMode::Color;
  if (n==7) s.mode=ChannelDisplayMode::Grayscale;
  const std::array<std::array<double,2>,3> ranges{{{1,201},{7,219},{11,241}}};
  for (int c=0;c<3;++c) {
    ChannelDisplayChannel ch; ch.range=ranges[c];
    ch.visible=n==6||n==8?c==1:c!=1;
    ch.lut.name.clear(); ch.lut.table=input_table(names.at(n-1)[c]);
    s.channels.push_back(std::move(ch));
  }
  return s;
}
ChannelDisplayState ordinary(int count=3, bool descending=false) {
  ChannelDisplayState s; s.version=ChannelDisplayVersion::V2;
  for (int c=0;c<count;++c) {
    ChannelDisplayChannel ch; ch.lut.name.clear();
    ch.lut.table=input_table(descending?"inv_red":"red"); s.channels.push_back(ch);
  }
  return s;
}
ChannelDisplayRequest select_min() { return {ChannelDisplayAction::SelectMin,{},{}}; }
ChannelDisplayRequest setting(ChannelDisplayProjection p=ChannelDisplayProjection::Sum) {
  auto s=ordinary(); s.projection=p; return {ChannelDisplayAction::Set,s,{}};
}
void position(Document &d,int t,int c,int z) {
  auto view=d.snapshot().display(); view.t=t; view.c=c; view.crosshair={2,1,z};
  view.zoom=2.5; view.planeZoom={1.5,2.5,3.5}; view.pan={{{1,2},{3,4},{5,6}}};
  view.orthogonal=true; view.linkedZoom=false;
  view.properties["unrelated"]=metadata_array({true,7,std::string("retained")}); d.set_display(view);
}
void set_state(Document &d,const ChannelDisplayState &state) {
  d.set_display(with_channel_display(d.snapshot().display(),d.snapshot().data().dataset,state));
}
bool same_display(const DisplayState &a,const DisplayState &b) {
  return a.t==b.t&&a.c==b.c&&a.crosshair==b.crosshair&&a.orthogonal==b.orthogonal&&
    a.linkedZoom==b.linkedZoom&&a.zoom==b.zoom&&a.planeZoom==b.planeZoom&&a.pan==b.pan&&
    a.revision==b.revision&&metadata_equal(a.properties,b.properties);
}
void untouched(Document &d,const DocumentSnapshot &before,const HistoryInfo &history) {
  auto now=d.snapshot();
  check(now.shares_data(before)&&now.data_identity()==before.data_identity(),"Preparation/refusal replaced owned data");
  check(same_display(now.display(),before.display())&&now.revisions().pixels==before.revisions().pixels&&
    now.revisions().metadata==before.revisions().metadata&&now.revisions().selection==before.revisions().selection&&
    now.revisions().display==before.revisions().display,"Preparation/refusal changed display or revisions");
  check(now.dirty()==before.dirty()&&d.history().undo_steps==history.undo_steps&&
    d.history().redo_steps==history.redo_steps,"Preparation/refusal changed dirty/history");
}
DocumentData rich_data() {
  DocumentData data{formula()}; data.metadata.title="kept";
  data.metadata.properties["opaque"]=metadata_array({true,9,std::string("unchanged")});
  data.metadata.extensions["vendor"]=metadata_object({{"retained",true}});
  data.metadata.record_layout["layout"]=std::string("kept");
  data.selection=Selection("rectangle",{{1,0},{4,2}}); data.previous_selection=Selection("ellipse",{{0,0},{2,2}});
  Roi roi{"positioned",Selection("rectangle",{{0,0},{2,2}})};
  roi.t=0;roi.c=0;roi.z=1;roi.attributes["tag"]=std::string("kept");data.rois.push_back(roi);
  data.overlays.push_back({{"kind",std::string("text")},{"text",std::string("kept")},{"t",0},{"c",0},{"z",1}});
  return data;
}
void populate_redo(Document &d) {
  auto edit=d.begin_edit(); auto p=*d.snapshot().data().dataset.plane(); p.pixels[0]+=1;
  edit.replace_plane({},p); d.commit(edit); d.undo(); d.mark_saved();
  check(d.history().redo_steps>0&&!d.snapshot().dirty(),"Fixture requires clean populated redo");
}
void effect(const ResolvedChannelDisplayCommand &command,const ChannelDisplayState &input,
            ChannelDisplayProjection projection,std::optional<int> navigation,
            const std::array<ChannelDisplayTable,3> &tables) {
  check(command.action()==ChannelDisplayAction::SelectMin&&command.version()==ChannelDisplayVersion::V2&&
    command.state()&&command.state()->version==ChannelDisplayVersion::V2,"SelectMin action/V2 identity");
  check(command.state()->mode==ChannelDisplayMode::Composite&&command.state()->projection==projection&&
    command.navigation_channel()==navigation,"Wrong resolved projection/mode/C effect");
  check(command.requires_captured_display()&&command.display_dependency_revision()==command.source().revisions().display,
    "Lost captured Display dependency");
  check(command.state()->channels.size()==3,"Channel membership changed");
  for(int c=0;c<3;++c) {
    const auto &ch=command.state()->channels[c];
    check(ch.visible==(input.mode==ChannelDisplayMode::Composite?input.channels[c].visible:true),"Mode entry visibility");
    check(ch.range==input.channels[c].range,"Requested range changed");
    check(ch.lut.name.empty()&&ch.lut.table&&*ch.lut.table==tables[c],"Complete frozen LUT byte mismatch");
  }
}
void transition() {
  const std::array<std::string,8> ids{{"N01.ascending-repeat","N02.inverted-retains-c2",
    "N03.mixed-max-then-min","N04.constant-active","N05.excluded-last-step",
    "N06.color-entry-flags","N07.owned-gray-min-min-save-fresh","N08.color-inverted-retains-c1"}};
  for(int n=1;n<=8;++n) {
    named(ids[n-1]); Document d(rich_data()); populate_redo(d); position(d,1,n==2?2:1,2);
    auto input=fixture(n); set_state(d,input); auto before=d.snapshot(); auto history=d.history();
    auto raw=fingerprint(before.data().dataset);
    auto command=resolve_channel_display_command(before,select_min());
    const bool transform=n==1||n==3||n==6||n==7;
    std::array<ChannelDisplayTable,3> expected{{*input.channels[0].lut.table,*input.channels[1].lut.table,*input.channels[2].lut.table}};
    if(n==1||n==6) expected={transformed_literal("red"),transformed_literal("yellow"),transformed_literal("cyan")};
    if(n==3) expected={transformed_literal("inv_red"),transformed_literal("yellow"),transformed_literal("custom_c")};
    if(n==7) expected={transformed_literal("red"),transformed_literal("inv_yellow"),transformed_literal("custom_c")};
    effect(command,input,n==3?ChannelDisplayProjection::Max:ChannelDisplayProjection::Min,
           transform?std::optional<int>(0):std::nullopt,expected);
    untouched(d,before,history);
    auto view=command_display_state(command,before);
    check(view.t==1&&view.c==(transform?0:(n==2?2:1))&&view.crosshair==before.display().crosshair&&
      view.zoom==before.display().zoom&&view.planeZoom==before.display().planeZoom&&view.pan==before.display().pan&&
      view.orthogonal==before.display().orthogonal&&view.linkedZoom==before.display().linkedZoom&&
      metadata_equal(view.properties.at("unrelated"),before.display().properties.at("unrelated")),"Publication changed T/Z/cameras/unrelated properties");
    d.set_display(view);
    check(d.snapshot().shares_data(before)&&fingerprint(d.snapshot().data().dataset)==raw&&
      d.snapshot().data().metadata==before.data().metadata&&d.snapshot().data().selection==before.data().selection&&
      d.snapshot().data().previous_selection==before.data().previous_selection&&d.snapshot().data().rois==before.data().rois&&
      metadata_equal(metadata_object(d.snapshot().data().overlays[0]),metadata_object(before.data().overlays[0])),"Publication changed source/annotations");
    check(d.snapshot().dirty()==before.dirty()&&d.history().undo_steps==history.undo_steps&&
      d.history().redo_steps==history.redo_steps&&d.snapshot().revisions().pixels==before.revisions().pixels&&
      d.snapshot().revisions().metadata==before.revisions().metadata&&d.snapshot().revisions().selection==before.revisions().selection,
      "Display intent changed source revisions/history/dirty");
    if(n==1||n==3||n==7) {
      auto repeat=resolve_channel_display_command(d.snapshot(),select_min());
      auto second=n==3?std::array<ChannelDisplayTable,3>{{input_table("inv_red"),input_table("yellow"),input_table("custom_c")}}:expected;
      effect(repeat,*command.state(),ChannelDisplayProjection::Min,n==3?std::optional<int>(0):std::nullopt,second);
      if(n==7) {
        // Same logical state after Save/Open or fresh construction must select
        // the same native branch; never infer Java processor-cache provenance.
        auto restored=decode_session(encode_session(SessionCapture{{d.snapshot()},{},{},{}}));
        auto &loaded=*restored.documents.at(0); auto saved=resolve_channel_display_command(loaded.snapshot(),select_min());
        effect(saved,*command.state(),ChannelDisplayProjection::Min,std::nullopt,expected);
        Document fresh(formula()); position(fresh,1,0,2); set_state(fresh,*command.state());
        auto clean=resolve_channel_display_command(fresh.snapshot(),select_min());
        effect(clean,*command.state(),ChannelDisplayProjection::Min,std::nullopt,expected);
        check(metadata_equal(saved.record().at("state"),repeat.record().at("state"))&&
          metadata_equal(clean.record().at("state"),repeat.record().at("state")),"Gray Save/Open/fresh state depends on hidden history");
      }
    }
  }
}
MetadataObject state_wire(const ChannelDisplayState &state) {
  MetadataArray channels;
  for(const auto &ch:state.channels) {
    MetadataObject lut;
    if(ch.lut.table) { MetadataArray rows;for(const auto &rgb:*ch.lut.table)
      rows.push_back(metadata_array({int(rgb[0]),int(rgb[1]),int(rgb[2])}));lut["table"]=metadata_array(rows); }
    else lut["name"]=ch.lut.name;
    channels.push_back(metadata_object({{"visible",ch.visible},{"range",metadata_array({ch.range[0],ch.range[1]})},{"lut",metadata_object(lut)}}));
  }
  MetadataObject result{{"schema",std::string("imagejxx.channel-display")},{"version",std::int64_t(state.version==ChannelDisplayVersion::V2?2:1)},
    {"mode",std::string(state.mode==ChannelDisplayMode::Composite?"composite":state.mode==ChannelDisplayMode::Color?"color":"grayscale")},
    {"channels",metadata_array(channels)}};
  if(state.version==ChannelDisplayVersion::V2) result["projection"]=std::string(state.projection==ChannelDisplayProjection::Max?"max":state.projection==ChannelDisplayProjection::Min?"min":"sum");
  return result;
}
MetadataObject record_wire(const DocumentSnapshot &source,const ChannelDisplayState &state,
                           const std::string &action="select_min",bool navigation=false) {
  auto shape=source.data().dataset.shape();
  MetadataObject descriptor{{"id",source.id()},{"pixels_revision",source.revisions().pixels},{"metadata_revision",source.revisions().metadata},
    {"shape",metadata_array({shape.t,shape.c,shape.z,shape.y,shape.x,shape.s})},
    {"type",std::string(source.data().dataset.pixel_type()==PixelType::UInt8?"uint8":source.data().dataset.pixel_type()==PixelType::UInt16?"uint16":"float32")},
    {"sample_policy",std::string("finite_only")}};
  return {{"schema",std::string("imagejxx.channel-display-command")},{"version",std::int64_t(2)},{"action",action},
    {"source",metadata_object(descriptor)},{"position",metadata_object({{"t",source.display().t},{"c",source.display().c},{"z",source.display().crosshair[2]}})},
    {"state",metadata_object(state_wire(state))},{"reset",metadata_object({{"channels",metadata_array({})},{"empty_finite",metadata_array({})}})},
    {"display_dependency",metadata_object({{"revision",source.revisions().display}})},
    {"navigation",navigation?metadata_object({{"c",0}}):MetadataValue(std::monostate{})}};
}
void records() {
  named("R01.authored-nine-field-wire"); Document d(formula());position(d,1,2,2);set_state(d,fixture(2));
  auto input=fixture(2);input.version=ChannelDisplayVersion::V2;input.projection=ChannelDisplayProjection::Min;
  auto good=record_wire(d.snapshot(),input);auto resolved=resolve_channel_display_command(d.snapshot(),select_min());
  check(metadata_equal(resolved.record(),good)&&resolved.record().size()==9,"Exact SelectMin wire differs from independently authored record");
  validate_channel_display_command_record(good);
  check(channel_display_command_from_record(d.snapshot(),good).action()==ChannelDisplayAction::SelectMin,"select_min decode action");
  named("R02.exact-envelope-version-reset-state");
  for(const auto &entry:good) {auto bad=good;bad.erase(entry.first);rejects([&]{validate_channel_display_command_record(bad);},"Missing SelectMin field");}
  auto bad=good;bad["extra"]=true;rejects([&]{validate_channel_display_command_record(bad);},"Extra SelectMin field");
  for(MetadataValue v:{MetadataValue(true),MetadataValue(2.0),MetadataValue(std::int64_t(3))}) {bad=good;bad["version"]=v;rejects([&]{validate_channel_display_command_record(bad);},"Bad SelectMin version");}
  bad=good;bad["version"]=std::int64_t(1);bad.erase("navigation");bad.erase("display_dependency");
  auto oldstate=input;oldstate.version=ChannelDisplayVersion::V1;oldstate.projection=ChannelDisplayProjection::Sum;bad["state"]=metadata_object(state_wire(oldstate));
  rejects([&]{validate_channel_display_command_record(bad);},"V1 select_min accepted");
  bad=changed(good,"state",[](auto &s){s["projection"]=std::string("sum");});rejects([&]{validate_channel_display_command_record(bad);},"SelectMin Sum accepted");
  bad=good;bad["state"]=std::monostate{};rejects([&]{validate_channel_display_command_record(bad);},"Null SelectMin state");
  bad=changed(good,"reset",[](auto &r){r["channels"]=metadata_array({0});r["empty_finite"]=metadata_array({false});});
  rejects([&]{validate_channel_display_command_record(bad);},"SelectMin recorded reset accepted");
  named("R03.strict-c0-null-effect-matrix");
  for(const std::string action:{"select_min","select_max"}) for(auto p:{ChannelDisplayProjection::Min,ChannelDisplayProjection::Max}) for(bool c0:{false,true}) {
    auto s=input;s.projection=p;auto record=record_wire(d.snapshot(),s,action,c0);
    const bool valid=c0||(action=="select_min"?p==ChannelDisplayProjection::Min:p==ChannelDisplayProjection::Max);
    if(valid) {validate_channel_display_command_record(record);auto replay=channel_display_command_from_record(d.snapshot(),record);
      check(replay.navigation_channel()==(c0?std::optional<int>(0):std::nullopt),"Valid C0/null effect changed");}
    else {rejects([&]{validate_channel_display_command_record(record);},"Inconsistent resolved effect accepted");
      rejects([&]{channel_display_command_from_record(d.snapshot(),record);},"Replay repaired inconsistent effect");
      rejects([&]{rebind_channel_display_command(d.snapshot(),record,setting());},"Rebind repaired inconsistent original effect");}
  }
  for(MetadataValue v:{metadata_object({{"c",1}}),metadata_object({{"c",true}}),metadata_object({{"c",0.0}}),metadata_object({{"c",-1}}),metadata_object({{"c",0},{"extra",0}}),MetadataValue(true)}) {
    bad=good;bad["navigation"]=v;rejects([&]{validate_channel_display_command_record(bad);},"Malformed/nonzero C effect accepted");
  }
  for(MetadataValue v:{MetadataValue(std::monostate{}),metadata_object({{"revision",true}}),metadata_object({{"revision",2.0}}),metadata_object({{"revision",-1}}),metadata_object({{"revision",0},{"extra",0}})}) {
    bad=good;bad["display_dependency"]=v;rejects([&]{validate_channel_display_command_record(bad);},"Malformed/null dependency accepted");
  }
  bad=good;bad["display_dependency"]=metadata_object({{"revision",std::uint64_t(0)}});validate_channel_display_command_record(bad);
  check(channel_display_command_from_record(d.snapshot(),bad).display_dependency_revision()==std::optional<std::uint64_t>(0),"Zero dependency treated as missing");
  named("R04.strict-source-position-and-other-action-effects");
  for(const std::string field:{"id","pixels_revision","metadata_revision"}) {bad=changed(good,"source",[&](auto &s){s[field]=true;});rejects([&]{validate_channel_display_command_record(bad);},"Boolean source number");}
  bad=changed(good,"source",[](auto &s){auto a=array(s.at("shape"));a[4]=std::numeric_limits<std::uint64_t>::max();s["shape"]=metadata_array(a);});rejects([&]{validate_channel_display_command_record(bad);},"Overflow source shape");
  bad=changed(good,"position",[](auto &p){p["c"]=0.0;});rejects([&]{validate_channel_display_command_record(bad);},"Floating captured position");
  bad=changed(good,"state",[](auto &s){s["mode"]=std::string("grayscale");});rejects([&]{validate_channel_display_command_record(bad);},"Noncanonical resolved mode");
  for(const std::string action:{"set","reset_range","disable"}) {bad=good;bad["action"]=action;rejects([&]{validate_channel_display_command_record(bad);},"Other action acquired SelectMin dependency");}
  named("R05.native-v3-preserves-historical-workflow");
  auto future=good;future["version"]=std::int64_t(37);future["future"]=true;
  MetadataArray workflow{metadata_object(good),metadata_object(future)};
  auto loaded=decode_session(encode_session(SessionCapture{{d.snapshot()},{},workflow,{}}));
  check(metadata_equal(metadata_array(loaded.workflow),metadata_array(workflow)),"Save/Open rewrote SelectMin/future workflow");
  check(loaded.documents[0]->id!=d.id,"Save/Open reused owner identity");
  rejects([&]{channel_display_command_from_record(loaded.documents[0]->snapshot(),object(loaded.workflow[0]));},"Save/Open silently rebound recorded intent");
  rejects([&]{validate_channel_display_command_record(object(loaded.workflow[1]));},"Future command interpreted");
}
void replay() {
  named("P01.fresh-display-guard-resolved-replay");Document d(formula());position(d,1,1,2);set_state(d,fixture(3));
  auto captured=d.snapshot();auto original=resolve_channel_display_command(captured,select_min());
  position(d,0,2,3);set_state(d,fixture(2));d.select(Selection("rectangle",{{0,0},{2,2}}));
  auto now=d.snapshot();auto history=d.history();
  rejects<RevisionConflict>([&]{command_display_state(original,now);},"Fresh SelectMin accepted changed Display");
  auto stored=channel_display_command_from_record(now,original.record());
  check(!stored.requires_captured_display()&&stored.display_dependency_revision()==captured.revisions().display&&metadata_equal(stored.record(),original.record()),"Replay lost historical resolved effect/dependency");
  auto view=command_display_state(stored,now);
  check(stored.state()->projection==ChannelDisplayProjection::Max&&view.t==0&&view.c==0&&view.crosshair==now.display().crosshair&&view.pan==now.display().pan&&view.planeZoom==now.display().planeZoom,"Replay reclassified or changed fresh T/Z/camera");untouched(d,now,history);
  named("P02.null-effect-retains-fresh-navigation");Document no_effect(formula());position(no_effect,1,2,2);set_state(no_effect,fixture(2));
  auto null_command=resolve_channel_display_command(no_effect.snapshot(),select_min());check(!null_command.navigation_channel(),"Inverted Min acquired C effect");
  position(no_effect,0,1,3);set_state(no_effect,fixture(1));auto current=no_effect.snapshot();
  auto replayed=channel_display_command_from_record(current,null_command.record());auto null_view=command_display_state(replayed,current);
  check(null_view.t==0&&null_view.c==1&&null_view.crosshair[2]==3&&replayed.state()->projection==ChannelDisplayProjection::Min&&
    replayed.state()->channels[0].lut.table==fixture(2).channels[0].lut.table,"Replay recomputed Min classifier/LUTs or captured position");
  named("P03.owner-pixels-metadata-guards");Document foreign(formula());
  rejects([&]{channel_display_command_from_record(foreign.snapshot(),original.record());},"Foreign replay accepted");
  rejects([&]{command_display_state(original,foreign.snapshot());},"Foreign publication accepted");
  auto edit=d.begin_edit();edit.set_title("changed");d.commit(edit);
  rejects<RevisionConflict>([&]{channel_display_command_from_record(d.snapshot(),original.record());},"Metadata-stale replay accepted");
  Document pixels(formula());set_state(pixels,fixture(1));auto pc=resolve_channel_display_command(pixels.snapshot(),select_min());
  auto pe=pixels.begin_edit();auto p=*pixels.snapshot().data().dataset.plane();p.pixels[0]+=1;pe.replace_plane({},p);pixels.commit(pe);
  rejects<RevisionConflict>([&]{channel_display_command_from_record(pixels.snapshot(),pc.record());},"Pixel-stale replay accepted");
  named("P04.same-revision-shape-type-and-request-set");Document same(formula());set_state(same,fixture(1));auto base=same.snapshot();auto command=resolve_channel_display_command(base,select_min());
  for(auto data:{formula(PixelType::UInt16),formula(PixelType::UInt8,3,0,6)}) {
    auto changed_data=std::make_shared<DocumentData>(base.data());changed_data->dataset=data;
    DocumentSnapshot inconsistent(same.identity,base.revisions(),changed_data,base.display(),base.dirty());
    rejects([&]{channel_display_command_from_record(inconsistent,command.record());},"Same-revision descriptor mismatch accepted");
  }
  auto direct=resolve_channel_display_command(base,setting(ChannelDisplayProjection::Min));position(same,0,2,3);
  auto output=command_display_state(direct,same.snapshot());
  check(output.c==2&&!direct.navigation_channel()&&!direct.requires_captured_display()&&direct.state()->channels[0].lut.table==ordinary().channels[0].lut.table,"Direct Set acquired SelectMin transition semantics");
  named("P05.same-c0-singleton-c8-c9");
  Document c0(formula());position(c0,1,0,2);set_state(c0,fixture(1));auto same_c=resolve_channel_display_command(c0.snapshot(),select_min());
  check(same_c.navigation_channel()==std::optional<int>(0)&&!std::holds_alternative<std::monostate>(same_c.record().at("navigation")),"Already C0 deliberate assignment erased");
  for(int count:{1,8,9}) {
    Document many(formula(PixelType::UInt8,count));position(many,1,count-1,2);auto s=ordinary(count,true);
    if(count==9)s.mode=ChannelDisplayMode::Color;
    for(auto &ch:s.channels)ch.visible=false;set_state(many,s);auto result=resolve_channel_display_command(many.snapshot(),select_min());
    const bool singleton=count==1;
    check(result.state()->channels.size()==static_cast<std::size_t>(count)&&result.state()->mode==(count==9?ChannelDisplayMode::Color:ChannelDisplayMode::Composite)&&
      result.state()->projection==(singleton?ChannelDisplayProjection::Max:ChannelDisplayProjection::Min)&&
      result.navigation_channel()==(singleton?std::optional<int>(0):std::nullopt),"Singleton gray/C8/C9 classification or fallback changed");
    for(int c=0;c<count;++c)check(result.state()->channels[c].visible==(count==9)&&result.state()->channels[c].range==s.channels[c].range&&
      result.state()->channels[c].lut.table==std::optional<ChannelDisplayTable>(singleton?transformed_literal("inv_red"):input_table("inv_red")),"Singleton/C8/C9 lost full flags/ranges/tables");
  }
}
class UnreadSource final : public PlaneSource {
public:
  DatasetShape sizes;mutable int reads=0,regions=0;
  explicit UnreadSource(DatasetShape s={2,3,4,3,5,1}):sizes(s){}
  DatasetShape shape()const override{return sizes;}
  PixelType pixel_type()const override{return PixelType::UInt8;}
  std::shared_ptr<const Image> read_plane(PlanePosition)const override{++reads;throw std::runtime_error("SelectMin read pixels");}
  Image read_region(PlanePosition,int,int,int,int)const override{++regions;throw std::runtime_error("SelectMin read region");}
};
void rebind() {
  named("B01.explicit-set-no-historical-effect");Document old(formula());position(old,1,1,2);set_state(old,fixture(3));
  auto original=resolve_channel_display_command(old.snapshot(),select_min());Document fresh(formula(PixelType::UInt16));position(fresh,0,2,3);
  auto set=rebind_channel_display_command(fresh.snapshot(),original.record(),setting());
  check(set.action()==ChannelDisplayAction::Set&&set.source().same_document(fresh.snapshot())&&set.state()->projection==ChannelDisplayProjection::Sum&&
    !set.navigation_channel()&&!set.display_dependency_revision()&&!set.requires_captured_display()&&command_display_state(set,fresh.snapshot()).c==2,"Set rebind inherited old SelectMin effect");
  named("B02.explicit-min-recomputes-new-capture");set_state(fresh,fixture(2));
  auto selected=rebind_channel_display_command(fresh.snapshot(),original.record(),select_min());
  check(selected.action()==ChannelDisplayAction::SelectMin&&selected.source().same_document(fresh.snapshot())&&selected.state()->projection==ChannelDisplayProjection::Min&&
    !selected.navigation_channel()&&selected.requires_captured_display()&&selected.display_dependency_revision()==fresh.snapshot().revisions().display&&
    selected.state()->channels[0].lut.table==fixture(2).channels[0].lut.table,"Explicit rebind retained old classification/table/C effect");
  named("B03.explicit-reset-and-disable-only");Document floating(formula(PixelType::Float32,3,100));position(floating,1,2,2);
  auto request=setting(ChannelDisplayProjection::Min);request.action=ChannelDisplayAction::ResetRange;request.reset_channels={2};
  auto reset=rebind_channel_display_command(floating.snapshot(),original.record(),request);
  check(reset.state()->channels[2].range==std::array<double,2>{210,218}&&reset.state()->projection==ChannelDisplayProjection::Min&&!reset.navigation_channel(),"Explicit Reset did not use fresh live values");
  auto disabled=rebind_channel_display_command(fresh.snapshot(),original.record(),{ChannelDisplayAction::Disable,{},{}});
  check(disabled.version()==ChannelDisplayVersion::V1&&disabled.record().size()==7&&!disabled.state()&&!disabled.navigation_channel(),"Disable inherited historical V2 effect");
  named("B04.full-original-before-new-source-callbacks");auto unread=std::make_shared<UnreadSource>();Document destination{Dataset(unread)};
  int calls=0;ExecutionControl control;control.checkpoint=[&]{++calls;throw std::runtime_error("Malformed original entered new intent");};
  request=setting();request.action=ChannelDisplayAction::ResetRange;
  std::vector<MetadataObject> invalid;auto bad=original.record();bad.erase("source");invalid.push_back(bad);
  bad=original.record();bad["navigation"]=std::monostate{};invalid.push_back(bad);
  bad=original.record();bad["display_dependency"]=std::monostate{};invalid.push_back(bad);
  invalid.push_back(changed(original.record(),"source",[](auto &s){s["pixels_revision"]=true;}));
  invalid.push_back(changed(original.record(),"state",[](auto &s){s["projection"]=std::string("sum");}));
  auto before=destination.snapshot();auto history=destination.history();
  for(const auto &record:invalid)rejects([&]{rebind_channel_display_command(destination.snapshot(),record,request,{},control);},"Malformed original repaired by rebind");
  check(calls==0&&unread->reads==0&&unread->regions==0,"Malformed original caused new callbacks/pixel reads");untouched(destination,before,history);
}
void limits() {
  named("L01.metadata-only-no-image-area-limit");auto unread=std::make_shared<UnreadSource>(DatasetShape{2,3,4,1000000,1000000,1});
  Document large{Dataset(unread)};set_state(large,fixture(1));int calls=0;ExecutionControl control;
  control.checkpoint=[&]{if(++calls>4096)throw std::runtime_error("Bounded C3 checkpoint budget exceeded");};
  auto command=resolve_channel_display_command(large.snapshot(),select_min(),{1,128ULL*1024*1024},control);
  check(command.state()->projection==ChannelDisplayProjection::Min&&command.navigation_channel()==std::optional<int>(0)&&calls>0&&
    calls<=4096&&unread->reads==0&&unread->regions==0,"Metadata intent read pixels or treated max_pixels as image area");
  named("L02.preflight-request-unknown-refusals-before-callback");auto before=large.snapshot();auto history=large.history();
  auto token=std::make_shared<CancellationToken>();token->cancel();control.cancellation=token;calls=0;
  for(auto cap:std::array<ChannelDisplayLimits,3>{{{0,128ULL*1024*1024},{1,0},{1,1}}})
    rejects([&]{resolve_channel_display_command(before,select_min(),cap,control);},"Invalid/tight limits skipped preflight");
  auto invalid=select_min();invalid.state=ordinary();rejects([&]{resolve_channel_display_command(before,invalid,{},control);},"SelectMin supplied state accepted");
  invalid=select_min();invalid.reset_channels={0};rejects([&]{resolve_channel_display_command(before,invalid,{},control);},"SelectMin reset channels accepted");
  check(calls==0&&unread->reads==0&&unread->regions==0,"Preflight called user/source");
  rejects<OperationCancelled>([&]{resolve_channel_display_command(before,select_min(),{1,128ULL*1024*1024},control);},"Valid pre-cancel ignored");
  check(calls==0,"Valid pre-cancel invoked callback");untouched(large,before,history);
  Document absent(formula());auto empty=absent.snapshot();auto eh=absent.history();
  rejects([&]{resolve_channel_display_command(empty,select_min());},"Absent display auto-enabled");untouched(absent,empty,eh);
  for(MetadataValue value:{metadata_object({{"schema",std::string("future")},{"version",1}}),metadata_object({{"schema",std::string("imagejxx.channel-display")},{"version",4}})}) {
    auto view=absent.snapshot().display();view.properties["channel_display"]=value;absent.set_display(view);auto current=absent.snapshot();auto h=absent.history();
    rejects([&]{resolve_channel_display_command(current,select_min());},"Unknown/future display interpreted");untouched(absent,current,h);
  }
  named("L03.c-dependent-working-bound-before-callback");auto many_source=std::make_shared<UnreadSource>(DatasetShape{1,64,1,1,1,1});Document many{Dataset(many_source)};
  auto s=ordinary(64);s.mode=ChannelDisplayMode::Color;set_state(many,s);calls=0;ExecutionControl bounded;bounded.checkpoint=[&]{++calls;};
  auto mb=many.snapshot();auto mh=many.history();rejects([&]{resolve_channel_display_command(mb,select_min(),{1,64*1024},bounded);},"C-dependent state/table/record cap admitted");
  check(calls==0&&many_source->reads==0&&many_source->regions==0,"Working-bound refusal called user/source");untouched(many,mb,mh);
  named("L04.callback-exception-and-mid-cancel-atomic");Document d(rich_data());populate_redo(d);position(d,1,1,2);set_state(d,fixture(1));before=d.snapshot();history=d.history();
  ExecutionControl throwing;throwing.checkpoint=[] {throw std::runtime_error("SelectMin checkpoint sentinel");};
  rejects<std::runtime_error>([&]{resolve_channel_display_command(before,select_min(),{},throwing);},"Checkpoint exception swallowed");untouched(d,before,history);
  auto during=std::make_shared<CancellationToken>();ExecutionControl cancel;cancel.cancellation=during;calls=0;
  cancel.checkpoint=[&]{++calls;during->cancel();};
  rejects<OperationCancelled>([&]{resolve_channel_display_command(before,select_min(),{},cancel);},"Mid-metadata cancel returned result");
  check(calls>0,"No metadata cancellation checkpoint");untouched(d,before,history);
  named("L05.freeze-all-inputs-before-callback");Document other(formula(PixelType::UInt16));
  auto mutable_source=d.snapshot();auto original_source=mutable_source;auto request=select_min();ChannelDisplayLimits cap;ExecutionControl mutable_control;
  auto baseline=resolve_channel_display_command(original_source,request);calls=0;
  mutable_control.checkpoint=[&]{if(++calls>4096)throw std::runtime_error("Checkpoint bound");mutable_source=other.snapshot();
    request.action=ChannelDisplayAction::Disable;request.state=ordinary();request.reset_channels={99};cap.max_pixels=0;cap.max_working_bytes=0;
    auto canceled=std::make_shared<CancellationToken>();canceled->cancel();mutable_control.cancellation=canceled;
    mutable_control.checkpoint=[] {throw std::runtime_error("Replacement checkpoint used");};};
  auto captured=resolve_channel_display_command(mutable_source,request,cap,mutable_control);
  check(calls>0&&captured.source().same_document(original_source)&&metadata_equal(captured.record(),baseline.record()),"Mutable callback changed captured request/source/limits/control");
  untouched(d,before,history);
}
} // namespace
int main(int argc,char **argv) {
  try {
    if(argc>2)throw std::invalid_argument("Usage: select_min [transition|records|replay|rebind|limits|all]");
    const std::string group=argc==2?argv[1]:"all";
    if(group!="all"&&group!="transition"&&group!="records"&&group!="replay"&&group!="rebind"&&group!="limits")throw std::invalid_argument("Unknown SelectMin group");
    if(group=="all"||group=="transition")transition();
    if(group=="all"||group=="records")records();
    if(group=="all"||group=="replay")replay();
    if(group=="all"||group=="rebind")rebind();
    if(group=="all"||group=="limits")limits();
    const std::size_t expected=group=="all"?27:group=="transition"?8:group=="rebind"?4:5;
    check(cases.size()==expected,"Frozen named membership changed");auto sorted=cases;std::sort(sorted.begin(),sorted.end());
    check(std::adjacent_find(sorted.begin(),sorted.end())==sorted.end(),"Duplicate named case");
    std::cout<<"channel_display_select_min "<<group<<": "<<checks<<" checks; "<<cases.size()<<" named cases\n";
    for(const auto &name:cases)std::cout<<name<<'\n';return 0;
  } catch(const std::exception &error) {
    std::cerr<<"channel_display_select_min after "<<checks<<" checks: "<<error.what()<<'\n';return 1;
  }
}

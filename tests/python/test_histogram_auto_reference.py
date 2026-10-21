"""Independent ImageJ 1.54r histogram/GUI Auto reference assertions.

The original Java handler, including its fallback and Recorder differences,
is the reference being observed. Passing these assertions grants no native
policy, product, commercial, OS-input or complete-family acceptance.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import struct

import pytest
import test_composite_max_reference as transport


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = "g057.histogram-auto-reference.001"
JAVA_CLASS = "ij.plugin.frame.HistogramAutoReference"
CASE_IDS = tuple(f"H{i:02d}" for i in range(1,17))
EXPECTED_COLLECTION_COUNT = 16
EXPECTED_COLLECTION_IDS = tuple(f"test_histogram_auto_reference[{case}]" for case in CASE_IDS)
STAGE = ROOT/"build/references/histogram-auto-imagej154r-001"
MAX_PAYLOAD,MAX_BUILD = 4*1024**2,32*1024**2
MAX_EXPANDED = 32*1024**2  #combined logical JSON, after BOTH lossless layers
TRANSPORT_READER = {"path":"tests/python/test_composite_max_reference.py","bytes":45146,
    "sha256":"17954cda6ae9a0ac7bff699785961923ab4ac5b30b58d87667c4d4958c8d3e61"}
BASE_ENTRY = {"path":"docs/parity/reviews/G033-histogram-auto-entry-001.json","bytes":53886,
    "sha256":"2f511fb6dcfc1f26c62060681eded31b6e21b025de37cd123777405b9d280c8e"}
ENTRIES = (BASE_ENTRY,
    {"path":"docs/parity/reviews/G033-histogram-auto-entry-002.json","bytes":9958,
     "sha256":"b623c9c42c7059a0d1eba6919a5917170fba00bdfd216c8621dbcca0e94f64b9"},
    {"path":"docs/parity/reviews/G033-histogram-auto-entry-003.json","bytes":10771,
     "sha256":"4ebbf78efbab889d23c07221b2af07664a24952d679ba0e2df253aa8c9782f67"})
# Set to the final source hold, never inferred from the consumed receipt.
PRODUCER_SHA = "9544a9bec82fb8f836500e520964d09cd02e8a0a6b72a88534d9b70854b50c93"
STAGE_COUNTS = (5,1,2,1,2,2,3,1,4,1,2,3,9,3,15,26)
PAINT_METHOD = "ImageCanvas.paint(Graphics2D) on private BufferedImage.TYPE_INT_RGB"
BOUNDS = {"case_count":16,"case_seconds":5,"prepare_parent_seconds":120,
    "capture_parent_seconds":120,"max_xy_pixels":4096,"max_scalar_samples_per_case":65536,
    "java_heap_mib":256,"combined_payload_bytes":MAX_PAYLOAD,"fresh_stage_bytes":MAX_BUILD,
    "max_live_reference_hosts":2}
DIVISORS = (0,5000,2500,1250,625,312,156,78,39,19,9)
DBL_MAX = float.fromhex("0x1.fffffffffffffp+1023")
F32_MAX = float.fromhex("0x1.fffffep+127")


def _f32(value):
    return struct.unpack("<f",struct.pack("<f",value))[0]


def _f64_hex(value):
    return struct.pack("<d",value).hex()


def _scalar(actual,expected):
    assert set(actual)=={"value","classification","ieee754_f64_le_hex"}
    if math.isnan(expected):
        assert actual["classification"]=="nan" and actual["value"] is None
        raw=bytes.fromhex(actual["ieee754_f64_le_hex"])
        assert len(raw)==8
        # Arithmetic NaN payload/sign is not fixed by the Java language. Its
        # actual bits remain observed; source NaN payloads are checked exactly.
        assert math.isnan(struct.unpack("<d",raw)[0])
    elif math.isinf(expected):
        assert actual["classification"]==("positive_infinity" if expected>0 else "negative_infinity")
        assert actual["value"] is None and actual["ieee754_f64_le_hex"]==_f64_hex(expected)
    else:
        assert actual["classification"]=="finite" and type(actual["value"]) in (int,float)
        assert actual["value"]==expected and actual["ieee754_f64_le_hex"]==_f64_hex(expected)
        assert _f64_hex(float(actual["value"]))==_f64_hex(expected)


def _jdiv(a,b):
    if b==0:
        return math.nan if a==0 else math.copysign(math.inf,a*b if b else a*math.copysign(1.,b))
    return a/b


def _jint(value):
    if math.isnan(value): return 0
    return min(2147483647,max(-2147483648,int(value))) if math.isfinite(value) else (2147483647 if value>0 else -2147483648)


def _jround(value):
    if math.isnan(value): return 0
    return math.floor(value+.5)


def _moments(rows):
    count=0;total=0.;squares=0.
    for value,n in rows:
        count+=n;total+=value*n;squares+=value*value*n
    mean=_jdiv(total,count)
    variance=_jdiv(count*squares-total*total,count)
    std=math.sqrt(variance/(count-1)) if count>1 and variance>0 else 0.
    return mean,std


def _histogram(values,dtype,*,bbox_values=None,calibration=None,spacing=(1.,1.)):
    """Test-owned raw-statistics model from Byte/Short/FloatStatistics.

    No producer/product helper supplies counts, min/max, bins or Auto limits.
    Values are the exact independently authored ROI members, in visit order.
    """
    finite=[v for v in values if math.isfinite(v) and (dtype!="float32" or -F32_MAX<=v<=F32_MAX)]
    hist16=None
    if dtype=="uint8":
        histogram=[0]*256
        for value in finite: histogram[int(value)]+=1
        minimum,maximum=(min(finite),max(finite)) if finite else (math.nan,math.nan)
        origin,top,width=0.,255.,1.
        n=len(finite);long_count=n
        rows=[(float(i),amount) for i,amount in enumerate(histogram)]
    elif dtype=="uint16":
        # ShortProcessor.getHistogram2 sizes by the complete bounding box,
        # including masked-out values, with a minimum length of256.
        bounding=list(values if bbox_values is None else bbox_values)
        hist16=[0]*max(256,1+max(map(int,bounding),default=0))
        for value in finite: hist16[int(value)]+=1
        minimum,maximum=(min(finite),max(finite)) if finite else (float(len(hist16)-1),0.)
        origin,top=float(minimum),float(maximum)
        width=(top-origin+1.)/256
        scale=1./width
        histogram=[0]*256
        for value,n in enumerate(hist16):
            if minimum<=value<=maximum:
                index=min(255,_jint(scale*(value-int(origin))))
                histogram[index]+=n
        n=len(finite);long_count=0
        rows=[(float(i),amount) for i,amount in enumerate(hist16) if minimum<=i<=maximum]
    else:
        assert dtype=="float32"
        minimum,maximum=(min(finite),max(finite)) if finite else (DBL_MAX,-DBL_MAX)
        origin,top=float(minimum),float(maximum)
        width=(top-origin)/256
        scale=_jdiv(256.,top-origin)
        histogram=[0]*256
        for value in finite: histogram[min(255,_jint(scale*(value-origin)))]+=1
        n=len(finite);long_count=0
        rows=[(float(value),1) for value in finite]
    if calibration is not None:
        assert dtype=="uint16"
        offset,slope=calibration
        rows=[(_f32(offset+slope*v),amount) for v,amount in rows]
        if finite: minimum,maximum=min(_f32(offset+slope*v) for v in finite),max(_f32(offset+slope*v) for v in finite)
        else: minimum,maximum=DBL_MAX,-DBL_MAX
    mean,std=_moments(rows)
    mode=max(range(256),key=lambda i:histogram[i])
    if dtype=="float32":
        dmode=origin+mode*width
        if width!=1.: dmode+=width/2.
    elif dtype=="uint16":
        occupied=range(len(hist16))
        dmode=float(max(occupied,key=lambda i:hist16[i]))
        if calibration is not None: dmode=_f32(calibration[0]+calibration[1]*dmode)
    else: dmode=float(mode)
    return {"class":"ij.process."+{"uint8":"ByteStatistics","uint16":"ShortStatistics","float32":"FloatStatistics"}[dtype],
        "histogram":histogram,"pixel_count":n,"long_pixel_count":long_count,
        "min":float(minimum),"max":float(maximum),"hist_min":origin,"hist_max":top,"bin_size":width,
        "mean":mean,"std_dev":std,"mode":mode,"dmode":dmode,"area":n*spacing[0]*spacing[1],
        "max_count":max(histogram),"histogram16":hist16}


def _statistics(actual,expected):
    assert set(actual)==set(expected)
    for key in ("class","histogram","pixel_count","long_pixel_count","mode","max_count"):
        assert actual[key]==expected[key],key
    histogram16=expected["histogram16"]
    assert actual["histogram16"] == (None if histogram16 is None else {
        "encoding":"int32_nonzero_pairs","length":len(histogram16),
        "nonzero":[[i,n] for i,n in enumerate(histogram16) if n]})
    assert len(actual["histogram"])==256 and all(type(n) is int and n>=0 for n in actual["histogram"])
    for key in ("min","max","hist_min","hist_max","bin_size","mean","std_dev","dmode","area"):
        _scalar(actual[key],expected[key])


def _auto(stats,previous):
    """The live Auto per-bin/divisor decision, not Enhance Contrast math."""
    assert previous in DIVISORS
    divisor=5000 if previous<10 else previous//2
    count=stats["pixel_count"];limit=count//10;threshold=count//divisor
    eligible=[i for i,n in enumerate(stats["histogram"]) if n<=limit and n>threshold]
    if not eligible:
        return {"divisor":0,"reset":True,"bounds":None}
    low,high=eligible[0],eligible[-1]
    bounds=[stats["hist_min"]+i*stats["bin_size"] for i in (low,high)]
    if bounds[0]==bounds[1]: bounds=[stats["min"],stats["max"]]
    return {"divisor":divisor,"reset":False,"bounds":bounds}


def _enhance(stats):
    """The separately recorded saturated=.35 cumulative-tail operation."""
    counts=stats["histogram16"] or stats["histogram"]
    threshold=int(stats["pixel_count"]*.35/200.)
    low=0;total=0
    for low,n in enumerate(counts):
        total+=n
        if total>threshold: break
    high=len(counts)-1;total=0
    for high in range(len(counts)-1,-1,-1):
        total+=counts[high]
        if total>threshold: break
    if high<=low: return None
    if stats["histogram16"] is not None: return [float(low),float(high)]
    return [stats["hist_min"]+i*stats["bin_size"] for i in (low,high)]


def _bits(dtype,values):
    code={"uint8":"B","uint16":"H","float32":"f"}[dtype]
    return b"".join(struct.pack("<"+code,v) for v in values).hex()


def _storage64(dtype,typed_hex):
    raw=bytes.fromhex(typed_hex)
    if dtype=="float32":
        result=bytearray()
        for (bits,) in struct.iter_unpack("<I",raw):
            exponent=(bits>>23)&255;mantissa=bits&0x7fffff
            if exponent==255 and mantissa:
                # Exact binary32->binary64 widening on the pinned JDK/x64;
                # retain the source sign, quiet bit and entire payload.
                result+=struct.pack("<Q",((bits>>31)<<63)|(0x7ff<<52)|(mantissa<<29))
            else: result+=struct.pack("<d",struct.unpack("<f",struct.pack("<I",bits))[0])
        return result.hex()
    return b"".join(struct.pack("<d",float(v)) for (v,) in struct.iter_unpack("<"+{"uint8":"B","uint16":"H"}[dtype],raw)).hex()


def _fixture(name, dtype="uint16", shape=(2,3,2,6,8), *, samples=None,
             position=(1,2,1), composite=True, roi=None, density="none",
             spatial=False, mode="composite", projection=None, ranges=None):
    """Literal input transport, independently spelled from entries001--003."""
    t_count,c_count,z_count,height,width=shape
    planes=[]
    for t in range(t_count):
        for z in range(z_count):
            for c in range(c_count):
                values=samples if samples is not None else [40*t+30*c+5*z+2*y+x for y in range(height) for x in range(width)]
                raw=_bits(dtype,values)
                planes.append({"t":t,"c":c,"z":z,"bits_hex":raw})
    return {"name":name,"dtype":dtype,"shape_tczyx":list(shape),"position_tcz":list(position),
        "image_kind":"CompositeImage" if composite else "ImagePlus","planes":planes,"roi":roi,
        "intensity_calibration":density,"spatial_calibration":({"spacing_xyz":[.5,1.25,2.5],
        "pixel_origin_xyz":[11,-7,19],"unit":"um","frame_interval":.75,"time_unit":"s"} if spatial else None),
        "mode":mode,"projection":projection,"ranges":ranges if ranges is not None else
        ([[0,255]]*c_count if dtype=="uint8" else [[1,201],[7,219],[11,241]] if composite else None),
        "tables":["red","green","blue"][:c_count] if composite else None,
        "visibility":[True,False,True] if composite else [True]*c_count}


def _action(label,operation="auto",**input_values):
    return {"label":label,"operation":operation,**input_values}


def _group(name,spec,actions,*,mode="Macro",other=None,compare=False,statistics_only=False):
    return {"name":name,"fixture":spec,"other":other,"recorder_mode":mode,
        "actions":actions,"compare_recorded_autos":compare,"statistics_only":statistics_only,
        "comparison_action":(_action("recorded_enhance_contrast.saturated_0_35" if name=="main" else "recorded_enhance_contrast_on_fresh_owner","recorded_comparisons") if compare else None)}


def _inputs():
    ordinary={"shape":(1,1,1,10,10),"position":(0,0,0),"composite":False}
    def byte(samples,name="main"):
        return _fixture(name,"uint8",samples=samples,**ordinary)
    def flat(dtype,samples,name="main"):
        return _fixture(name,dtype,(1,1,1,1,len(samples)),samples=samples,position=(0,0,0),composite=False)
    distribution=[1]+[2]*10+[3]*11+[4]*78
    rows=[
        [_group("main",_fixture("main","uint8",(1,1,1,64,64),samples=[10]+[20]*3+[30]*200+[40]*200+[50]*200+[60]*3492,
             position=(0,0,0),composite=False),[_action("auto.1"),_action("auto.2"),_action("auto.3"),_action("reset","reset"),_action("auto.after_reset")])],
        [_group("main",byte(distribution),[_action("auto")],compare=True)],
        [_group("main",byte([7]*100),[_action("auto"),_action("auto.repeated")])],
        [_group("main",byte([10]*8+[200]*92),[_action("auto")])],
        [_group("main",flat("uint16",[1000+257*i for i in range(16)]),[_action("auto"),_action("redraw","redraw")])],
        [_group("main",flat("float32",[-2.5+.5*i for i in range(16)]),[_action("auto"),_action("redraw","redraw")])],
        [_group("main",_fixture("main",roi={"kind":"rectangle","xywh":[1,1,6,4]}),[_action("auto"),_action("navigate_t0c0z0","navigate",position_tcz=[0,0,0]),_action("auto.after_navigation")])],
        [_group("main",_fixture("main",roi={"kind":"holed_area","outer_xywh":[2,1,4,4],"hole_xywh":[3,2,2,2]}),[_action("auto")])],
        [_group("main",_fixture("main",roi={"kind":"point","xy":[2,2]}),[_action("auto.point"),_action("reset","reset"),_action("set_line_roi","set_roi",roi={"kind":"line","xyxy":[1,1,6,4]}),_action("auto.line")])],
        [_group("main",_fixture("main",roi={"kind":"empty_area","anchor":[2,1],"constructor":"new ShapeRoi(2,1,new java.awt.geom.Area())"}),[_action("auto")])],
        [_group("main",flat("float32",[math.nan,math.nan,-0.,0.,-5.,-4.,-3.,-2.,-1.,1.,2.,3.,4.,5.,6.,7.]),[_action("auto"),_action("redraw","redraw")])],
        [_group("main",flat("float32",[math.nan]*16),[_action("statistics","statistics"),_action("auto")]),
         _group("mixed_statistics_only",flat("float32",[math.nan,-math.inf,math.inf,7.],"mixed_statistics_only"),[_action("statistics","statistics")],statistics_only=True)],
    ]
    # Source nonfinite payloads are fixed input bytes, independent of Python's
    # arithmetic NaN. All finite samples above are independently literal.
    rows[10][0]["fixture"]["planes"][0]["bits_hex"]="4523c17f5634c2ff0000008000000000"+_bits("float32",[-5.,-4.,-3.,-2.,-1.,1.,2.,3.,4.,5.,6.,7.])
    rows[11][0]["fixture"]["planes"][0]["bits_hex"]="4523c17f"*16
    rows[11][1]["fixture"]["planes"][0]["bits_hex"]="4523c17f000080ff0000807f0000e040"
    h13=[]
    for name,composite,density in (("ordinary_unsigned",False,"none"),("composite_unsigned",True,"none"),("ordinary_signed16",False,"signed16")):
        spec=_fixture(name,shape=(2,3,2,10,10),samples=[7]*100,position=(1,1,1)) if composite else _fixture(name,samples=[7]*100,density=density,**ordinary)
        h13.append(_group(name,spec,[_action("auto.automatic"),_action("reset_cycle_and_set_12bit","reset_then_12bit"),_action("auto.12bit")]))
    rows.append(h13)
    rows.append([_group(name,_fixture(name,density=density,spatial=True),[_action("auto.raw" if density=="none" else "auto.intensity_calibrated")])
                 for name,density in (("raw","none"),("linear","linear"),("signed16","signed16"))])
    rows.append([_group(name,_fixture(name,position=(1,1,1),mode=mode,projection=projection),
                 [_action("auto.active",shift=False),_action("restore_fixture","restore_fixture"),_action("auto.shift_all",shift=True)])
                 for name,mode,projection in (("composite_sum","composite",None),("composite_max","composite","Max"),("composite_min","composite","Min"),("color_max","color","Max"),("gray_max","grayscale","Max"))])
    contexts=[]
    for mode in ("Macro","Java"):
        spec=_fixture(mode+".original","uint8",(2,3,2,10,10),position=(1,1,1))
        other=copy.deepcopy(spec);other["name"]=mode+".other"
        for owner,offset in ((spec,0),(other,40)):
            for plane in owner["planes"]:
                plane["bits_hex"]=bytes(value+5*plane["t"]+10*plane["c"]+2*plane["z"]+offset for value in distribution).hex()
        actions=[_action("auto.initial"),_action("change_area_roi","set_roi",roi={"kind":"rectangle","xywh":[1,1,8,8]}),
            _action("auto.after_roi"),_action("set_numeric_range","numeric_set",range=[0,200],redraw=True),_action("auto.after_set"),
            _action("public_update","public_update"),_action("auto.after_update"),_action("navigate_czt","navigate",position_tcz=[0,2,0],redraw=True),
            _action("auto.after_navigation"),_action("owner_away_back","owner_away_back"),_action("auto.after_activation"),_action("reset","reset"),_action("auto.after_reset")]
        contexts.append(_group(mode,spec,actions,mode=mode,other=other,compare=True))
    rows.append(contexts)
    return {case:{"id":case,"groups":groups} for case,groups in zip(CASE_IDS,rows)}


INPUTS=_inputs()


def _values(spec,position):
    t,c,z=position
    plane=next(p for p in spec["planes"] if [p["t"],p["c"],p["z"]]==[t,c,z])
    return [v for (v,) in struct.iter_unpack("<"+{"uint8":"B","uint16":"H","float32":"f"}[spec["dtype"]],bytes.fromhex(plane["bits_hex"]))]


def _members(spec,position,roi):
    values=_values(spec,position);h,w=spec["shape_tczyx"][-2:]
    cells=list(range(w*h));bounds=[0,0,w,h]
    if roi and roi["kind"] in {"rectangle","holed_area","empty_area"}:
        if roi["kind"]=="empty_area":
            bounds=[2,1,1,1];cells=[]
        else:
            bounds=roi["xywh"] if roi["kind"]=="rectangle" else roi["outer_xywh"]
            x,y,bw,bh=bounds
            cells=[j*w+i for j in range(y,y+bh) for i in range(x,x+bw)]
            if roi["kind"]=="holed_area":
                hx,hy,hw,hh=roi["hole_xywh"]
                cells=[p for p in cells if not(hx<=p%w<hx+hw and hy<=p//w<hy+hh)]
    x,y,bw,bh=bounds
    return [values[i] for i in cells],[values[j*w+i] for j in range(y,y+bh) for i in range(x,x+bw)],bounds


def _raw_expected(spec,position,roi):
    values,box,_=_members(spec,position,roi)
    return _histogram(values,spec["dtype"],bbox_values=box)


def _calibration(actual,spec):
    spatial=spec["spatial_calibration"]
    for key,expected in (("spacing_xyz",spatial["spacing_xyz"] if spatial else [1.,1.,1.]),
                         ("pixel_origin_xyz",spatial["pixel_origin_xyz"] if spatial else [0.,0.,0.])):
        assert len(actual[key])==3
        for value,want in zip(actual[key],expected): _scalar(value,float(want))
    unit="\u00b5m" if spatial else "pixel"
    assert all(actual[key]==unit for key in ("unit","x_unit","y_unit","z_unit"))
    _scalar(actual["frame_interval"],.75 if spatial else 0.)
    assert actual["time_unit"]==("s" if spatial else "sec")
    density=spec["intensity_calibration"]
    assert actual["signed16"] is (density=="signed16") and actual["calibrated"] is (density!="none")
    assert actual["function"]==(20 if density=="none" else 0)
    assert actual["value_unit"]==("a.u." if density=="linear" else "Gray Value")
    coefficients=None if density=="none" else [5.,2.] if density=="linear" else [-32768.,1.]
    if coefficients is None: assert actual["coefficients"] is None
    else:
        assert len(actual["coefficients"])==2
        for observed,want in zip(actual["coefficients"],coefficients): _scalar(observed,want)


def _roi(actual,spec):
    if spec is None:
        assert actual is None;return
    kind=spec["kind"]
    expected={"rectangle":(0,"ij.gui.Roi",True,False,spec.get("xywh")),
        "holed_area":(9,"ij.gui.ShapeRoi",True,False,spec.get("outer_xywh")),
        "empty_area":(9,"ij.gui.ShapeRoi",True,False,[2,1,1,1]),
        "point":(10,"ij.gui.PointRoi",False,False,[2,2,1,1]),
        "line":(5,"ij.gui.Line",False,True,[1,1,6,4])}[kind]
    typ,cls,area,line,bounds=expected
    assert type(actual["identity"]) is int and actual["identity"]!=0
    assert (actual["type"],actual["class"],actual["is_area"],actual["is_line"],actual["bounds_xywh"])==(typ,cls,area,line,bounds)
    assert actual["position"]==0 and actual["czt_position"]==[0,0,0]
    assert actual["masks_same_identity"] is True
    assert actual["mask_first"]==actual["mask_second"]
    if kind in {"empty_area","holed_area"}: assert actual["shape_empty"] is (kind=="empty_area")
    else: assert actual["shape_empty"] is None
    if kind in {"rectangle","line"}: assert actual["mask_first"] is None
    elif kind=="empty_area":
        assert actual["float_polygon_xy"]==[]
        assert actual["mask_first"]["shape"]==[1,1] and actual["mask_first"]["bits_hex"]=="00"
    elif kind=="holed_area":
        assert actual["mask_first"]["shape"]==[4,4]
        assert actual["mask_first"]["bits_hex"]==bytes([255,255,255,255,255,0,0,255,255,0,0,255,255,255,255,255]).hex()
    # Vertex order of Java2D paths is observational; all relevant raster cells
    # and declared bounding boxes are independently fixed above.
    for point in actual["float_polygon_xy"]:
        assert len(point)==2
        for value in point: _scalar(value,float(value["value"]))


def _source(actual,spec,roi):
    assert actual["shape_tczyx"]==spec["shape_tczyx"]
    assert actual["bit_depth"]=={"uint8":8,"uint16":16,"float32":32}[spec["dtype"]]
    assert len(actual["planes"])==len(spec["planes"])
    c_count,z_count=spec["shape_tczyx"][1:3]
    for observed,expected in zip(actual["planes"],spec["planes"]):
        assert observed=={**expected,"stack_index":(expected["t"]*z_count+expected["z"])*c_count+expected["c"]+1,
            "storage_f64_hex":_storage64(spec["dtype"],expected["bits_hex"])}
    _calibration(actual["calibration"],spec);_roi(actual["roi"],roi)
    # getProperties is the legacy object-property map, not setProp's separate
    # persistent string-property map. Projection is checked in display below.
    assert actual["properties"]=={}


def _hidden(flags):
    assert set(flags)=={"visible","showing","displayable","enabled"}
    assert flags["visible"] is False and flags["showing"] is False
    assert type(flags["displayable"]) is bool and type(flags["enabled"]) is bool


def _thread(value,*,edt=None,alive=True):
    assert value["identity"]!=0 and value["id"]>0 and value["name"]
    assert value["alive"] is alive
    assert value["state"] in {"NEW","RUNNABLE","BLOCKED","WAITING","TIMED_WAITING","TERMINATED"}
    if edt is not None: assert value["event_dispatch_thread"] is edt
    assert type(value["observer_event_dispatch_thread"]) is bool
    assert type(value["current_thread_identity"]) is int and value["current_thread_identity"]!=0


def _host(actual,spec,position,*,painted=None,label=None):
    h,w=spec["shape_tczyx"][-2:];t,c,z=position
    assert actual["image_id"]!=0 and actual["image_identity"]!=0
    assert actual["window_class"]==JAVA_CLASS+("$HiddenStackWindow" if len(spec["planes"])>1 else "$HiddenImageWindow")
    assert actual["canvas_class"]=="ij.gui.ImageCanvas"
    assert actual["window_image_identity"]==actual["canvas_image_identity"]==actual["image_identity"]
    assert actual["window_canvas_identity"]==actual["canvas_identity"]!=0
    assert actual["canvas_parent_identity"]==actual["window_identity"]!=0
    assert actual["current_window_is_host"] is True and actual["current_image_is_source"] is True
    assert actual["position_tcz"]==list(position)
    assert actual["current_slice_index"]==(t*spec["shape_tczyx"][2]+z)*spec["shape_tczyx"][1]+c
    many=len(spec["planes"])>1
    assert actual["open_as_hyperstack"] is many
    assert actual["displayed_hyperstack"] is many and actual["window_hyperstack"] is many
    assert actual["selector_position_czt"]==([c,z,t] if many else [None,None,None])
    _hidden(actual["window"])
    canvas=actual["canvas"];_scalar(canvas["magnification"],2.)
    assert canvas["source_rect_xywh"]==[0,0,w,h] and canvas["drawing_wh"]==[2*w,2*h]
    assert all(type(x) is bool for x in canvas["flags"].values())
    paint=actual["offscreen_paint"]
    assert paint["method"]==PAINT_METHOD and paint["surface_wh"]==[2*w,2*h]
    if painted is not None:
        assert paint["count"]==painted and paint["last_label"]==label
    else: assert type(paint["count"]) is int and paint["count"]>=0


def _recorder(actual,mode,text):
    assert actual["class"]=="ij.plugin.frame.Recorder" and actual["identity"]!=0
    _hidden(actual["flags"])
    assert actual["recording"] is True and actual["script_mode"] is (mode=="Java")
    assert actual["mode"]==mode and actual["text"]==text and actual["choice_identity"]!=0
    event=actual["mode_dispatch"]
    assert event["event_id"]==701 and event["state_change"]==1
    assert event["source_identity"]==actual["choice_identity"] and event["event_identity"]!=0
    assert event["selected_item"]==mode and event["original_listener_registered"] is True
    assert event["listeners"]==["ij.plugin.frame.Recorder"] and event["event_dispatch_thread"] is True


def _ca(actual,*,idle=True):
    assert actual["identity"]!=0 and type(actual["auto_threshold"]) is int and actual["auto_threshold"] in DIVISORS
    for key in ("setups","setup_images","new_images","resets","worker_calls","no_pending_calls","assigned_sequence","completed_sequence"):
        assert type(actual[key]) is int and actual[key]>=0
    assert actual["worker_calls"]>=actual["no_pending_calls"]
    assert actual["assigned_sequence"]>=actual["completed_sequence"]
    assert actual["done"] is False
    _hidden(actual["dialog"]);_thread(actual["worker"])
    if idle:
        assert not any(actual[k] for k in ("do_auto","do_reset","do_set","do_apply_lut"))
        assert all(actual[k]==-1 for k in ("min_slider_value","max_slider_value","brightness_value","contrast_value"))
    for key in ("min","max","default_min","default_max","previous_min","previous_max"):
        value=actual[key]
        _scalar(value,float(value["value"]) if value["classification"]=="finite" else
                math.nan if value["classification"]=="nan" else math.inf if value["classification"]=="positive_infinity" else -math.inf)


def _plot(actual,spec,position,roi):
    assert actual["class"]==JAVA_CLASS+"$ActualContrastPlot" and actual["identity"]!=0
    assert actual["set_histogram_declaring_class"]=="ij.plugin.frame.ContrastPlot"
    assert actual["entered"]==actual["super_returned"] and actual["entered"]>0
    assert actual["log"] is False
    values,box,_=_members(spec,position,roi)
    if roi and roi["kind"]=="point":
        # ImagePlus.getStatistics:1146--1151 assigns a single PointRoi a
        # one-pixel box. getRawStatistics:1112--1116 instead resets non-area
        # ROI, so Auto's independent _raw_expected remains the full plane.
        x,y=roi["xy"];width=spec["shape_tczyx"][-1]
        values=[_values(spec,position)[y*width+x]];box=values
    density=spec["intensity_calibration"]
    calibration=None if density=="none" else (5.,2.) if density=="linear" else (-32768.,1.)
    spacing=(.5,1.25) if spec["spatial_calibration"] else (1.,1.)
    expected=_histogram(values,spec["dtype"],bbox_values=box,calibration=calibration,spacing=spacing)
    # An unsigned fixed-range histogram uses StackStatistics, independently
    # distinguished from the raw ShortStatistics consumed by Auto itself.
    if actual["input_statistics"]["class"]=="ij.process.StackStatistics":
        assert spec["dtype"]=="uint16" and density!="signed16"
        assert all(v==7 for v in values) and len(values)==100
        counts=[100]+[0]*255
        expected={**expected,"class":"ij.process.StackStatistics","histogram":counts,
            "histogram16":None,"long_pixel_count":100,"hist_min":0.,"hist_max":4096.,
            "bin_size":4097./256,"mode":0,"dmode":0.,"max_count":100,"area":100.}
    _statistics(actual["input_statistics"],expected)
    counts=expected["histogram"].copy();mode=expected["mode"]
    second=max(n for i,n in enumerate(counts) if i!=mode)
    maximum=expected["max_count"]
    if maximum>2*second and second!=0:
        maximum=int(second*1.5);counts[mode]=maximum
    assert actual["histogram_after"]==counts and actual["hmax"]==maximum


def _state(actual,spec,position,roi,*,mode=None,text=None,painted=None,label=None,plot_current=False):
    image=actual["image"]
    assert image["class"]==JAVA_CLASS+("$ObservedCompositeImage" if spec["image_kind"]=="CompositeImage" else "$ObservedImagePlus")
    assert image["shape_tczyx"]==spec["shape_tczyx"] and image["position_tcz"]==list(position)
    assert image["current_slice_index"]==actual["host"]["current_slice_index"]
    assert image["id"]==actual["host"]["image_id"] and image["identity"]==actual["host"]["image_identity"]
    assert image["type"]=={"uint8":0,"uint16":1,"float32":2}[spec["dtype"]]
    assert image["bit_depth"]=={"uint8":8,"uint16":16,"float32":32}[spec["dtype"]]
    assert image["changes"] is False and image["properties"]=={}
    _calibration(image["calibration"],spec);_roi(actual["roi"],roi)
    _host(actual["host"],spec,position,painted=painted,label=label)
    t,c,z=position
    bits=next(p["bits_hex"] for p in spec["planes"] if [p["t"],p["c"],p["z"]]==list(position))
    for key in ("processor_before_raw_statistics","processor_after_raw_statistics"):
        p=actual[key]
        assert p["class"]=="ij.process."+{"uint8":"ByteProcessor","uint16":"ShortProcessor","float32":"FloatProcessor"}[spec["dtype"]]
        assert p["identity"]!=0 and p["pixels_identity"]==p["source_pixels_identity"]!=0
        assert p["pixels_are_source"] is True and p["bits_hex"]==bits
        assert len(bytes.fromhex(p["lut_rgb_interleaved_hex"]))==768 and type(p["inverted_lut"]) is bool
        density=spec["intensity_calibration"]
        table=p["calibration_table"]
        if density=="none": assert table is None
        else:
            values=[5.+2*i for i in range(65536)] if density=="linear" else [float(i-32768) for i in range(65536)]
            assert table=={"length":65536,"bits_sha256":hashlib.sha256(bytes.fromhex(_bits("float32",values))).hexdigest()}
    a,b=actual["processor_before_raw_statistics"],actual["processor_after_raw_statistics"]
    for key in ("identity","class","pixels_identity","source_pixels_identity","pixels_are_source","bits_hex","min","max","lut_rgb_interleaved_hex","inverted_lut","calibration_table"):
        assert a[key]==b[key],key
    _,_,bounds=_members(spec,position,roi)
    assert b["roi_xywh"]==bounds
    if roi and roi["kind"] in {"empty_area","holed_area"}:
        assert b["mask"]["bits_hex"]==actual["roi"]["mask_first"]["bits_hex"]
        assert b["mask"]["shape"]==actual["roi"]["mask_first"]["shape"]
    else: assert b["mask"] is None
    assert actual["raw_statistics"]["error"] is None
    _statistics(actual["raw_statistics"]["statistics"],_raw_expected(spec,position,roi))
    display=actual["display"]
    assert display["default16bit_range"] in (0,12)
    assert display["projection"]==spec["projection"]
    if spec["image_kind"]=="ImagePlus":
        assert display["mode"] is None and display["visibility"] is None and display["channel_luts"] is None
    else:
        assert display["mode"]=={"composite":1,"color":2,"grayscale":3}[spec["mode"]]
        assert len(display["channel_luts"])==spec["shape_tczyx"][1] and len(display["visibility"])==8
        assert all(type(v) is bool for v in display["visibility"])
        for i,lut in enumerate(display["channel_luts"]):
            assert lut["channel"]==i and lut["rgb_interleaved_hex"]==_table_hex(i)
    if mode is not None:
        _recorder(actual["recorder"],mode,text);_ca(actual["contrast_adjuster"])
        if plot_current: _plot(actual["plot"],spec,position,roi)
    else: assert actual["recorder"] is None and actual["contrast_adjuster"] is None and actual["plot"] is None
    assert type(actual["raw_statistics_call_count"]) is int and actual["raw_statistics_call_count"]>0


def _table_hex(channel):
    return bytes(component for i in range(256) for component in ([i,0,0] if channel==0 else [0,i,0] if channel==1 else [0,0,i])).hex()


def _number(value):
    return float(value["value"]) if value["classification"]=="finite" else math.nan if value["classification"]=="nan" else math.inf if value["classification"]=="positive_infinity" else -math.inf


def _range(actual,pair):
    assert len(pair)==2
    _scalar(actual["range_min"],float(pair[0]));_scalar(actual["range_max"],float(pair[1]))


def _reset_bounds(spec,position,bits):
    if spec["dtype"]=="uint8": return [0.,255.]
    if spec["dtype"]=="uint16" and bits and (spec["image_kind"]=="CompositeImage" or spec["intensity_calibration"]!="signed16"):
        return [0.,float((1<<bits)-1)]
    values=[v for v in _values(spec,position) if not math.isnan(v)]
    return [min(values),max(values)] if values else [math.nan,math.nan]


def _getter_bounds(spec,pair):
    if spec["dtype"]=="float32": return [_f32(v) for v in pair]
    return [float(_jround(v)) for v in pair]


def _button(actual,operation,before,after,source_id):
    assert actual["operation"]==operation and actual["sequence"]>0
    assert actual["monitor_held"] is True and actual["idle_worker"]["state"]=="WAITING"
    _thread(actual["idle_worker"],edt=False)
    assert actual["idle_worker"]["observer_event_dispatch_thread"] is True
    assert actual["handler_declaring_class"]==actual["worker_run_declaring_class"]=="ij.plugin.frame.ContrastAdjuster"
    button=actual["button"]
    assert button["label"]==("Auto" if operation=="auto" else "Reset")
    assert button["original_listener_registered"] is True and button["action_handler_super_declaring_class"]=="ij.plugin.frame.ContrastAdjuster"
    assert button["listeners"]==[JAVA_CLASS+"$ObservedAdjuster"] and button["flags"]["enabled"] is True
    assert actual["event_id"]==1001 and actual["event_source_identity"]==button["identity"]!=0
    assert actual["event_identity"]!=0 and actual["event_dispatch_thread"] is True
    assert actual["event_command"]==button["label"]
    assert actual["acknowledged_sequence"]==actual["sequence"]
    assert actual["before"]["identity"]==before["identity"]==after["identity"]
    pending=actual["after_listener"]
    assert pending["do_auto"] is (operation=="auto") and pending["do_reset"] is (operation=="reset")
    assert pending["assigned_sequence"]==actual["sequence"]
    call=actual["worker_call"]
    assert call["sequence"]==actual["sequence"] and call["pending"] is True
    assert call["source_image_id"]==source_id and call["error"] is None and call["super_returned"] is True
    _thread(call["thread"],edt=False)
    assert call["thread"]["identity"]==before["worker"]["identity"]==after["worker"]["identity"]
    assert call["after"]["worker_calls"]==call["before"]["worker_calls"]
    assert after["worker_calls"]==before["worker_calls"]+1
    assert after["completed_sequence"]==after["assigned_sequence"]==actual["sequence"]
    _ca(after)


def _rgb(actual,spec,position,display):
    h,w=spec["shape_tczyx"][-2:]
    assert actual["shape"]==[h,w,3] and len(bytes.fromhex(actual["rgb_hex"]))==h*w*3
    assert len(actual["canvas_rgb_sha256"])==64 and all(c in "0123456789abcdef" for c in actual["canvas_rgb_sha256"])
    # The test-owned mapper uses exactly the pinned source's integer/floating
    # conversion. Source bits and each stored range/table are checked separately.
    def index(value,dtype,pair):
        low,high=pair
        if dtype=="uint8":
            if value<low:return 0
            if value>high:return 255
            return max(0,min(255,_jint(_jdiv((value-_jround(low))*256.,high-low))))
        if dtype=="uint16":
            low,high=_jround(low),_jround(high)
            return max(0,min(255,_jint(_jdiv(max(0,value-low)*256.,high-low+1.)+.5)))
        low,high=_f32(low),_f32(high)
        return max(0,min(255,_jint(_jdiv((value-low)*255.,high-low)+.5)))
    current=position[1];dtype=spec["dtype"]
    if spec["image_kind"]=="ImagePlus":
        pair=[_number(display[k]) for k in ("range_min","range_max")]
        pixels=[channel for v in _values(spec,position) for channel in [index(v,dtype,pair)]*3]
    else:
        count=spec["shape_tczyx"][1];samples=[_values(spec,[position[0],c,position[2]]) for c in range(count)]
        pairs=[[_number(x["min"]),_number(x["max"])] for x in display["channel_luts"]]
        pixels=[]
        for i in range(w*h):
            if spec["mode"]=="grayscale":
                pixels.extend([index(samples[current][i],dtype,pairs[current])]*3)
            elif spec["mode"]=="color":
                v=index(samples[current][i],dtype,pairs[current]);pixels.extend([v if c==current else 0 for c in range(3)])
            else:
                rgb=[index(samples[c][i],dtype,pairs[c]) if display["visibility"][c] else 0 for c in range(count)]
                # RGB primaries occupy distinct components. Max and SUM agree;
                # Min includes each contributing channel's zero components.
                if spec["projection"]=="Min" and sum(display["visibility"][:count])>1: pixels.extend([0,0,0])
                else: pixels.extend(rgb+[0]*(3-count))
    assert actual["rgb_hex"]==bytes(pixels).hex()


def _record_delta(mode,operation,decision=None):
    if operation=="auto":
        if decision["reset"]: return ""
        return 'IJ.run(imp, "Enhance Contrast", "saturated=0.35");\n' if mode=="Java" else 'run("Enhance Contrast", "saturated=0.35");\n'
    if operation in {"reset","reset_then_12bit"}:
        return "IJ.resetMinAndMax(imp);\n" if mode=="Java" else "resetMinAndMax;\n"
    return ""


def _close_host(actual,spec):
    assert actual["before"]["image_id"]!=0
    _hidden(actual["window_after"])
    if len(spec["planes"])>1:
        assert actual["stack_worker"] is not None
        # StackWindow.close signals the original worker. The supervisor owns
        # process teardown; no claim of a join that this observer did not make.
        assert actual["stack_worker"]["identity"]!=0
    else: assert actual["stack_worker"] is None


def _group_stages(row,group,offset):
    spec=group["fixture"];mode=group["recorder_mode"] if not group["statistics_only"] else None
    position=spec["position_tcz"].copy();roi=copy.deepcopy(spec["roi"]);text="";painted=0;last_label=None
    previous=None;comparisons=[];buttons=[]
    for index,action in enumerate(group["actions"]):
        stage=row["stages"][offset+index]
        assert stage["ordinal"]==offset+index and stage["context"]==group["name"] and stage["fixture"]==spec["name"]
        assert stage["label"]==action["label"] and stage["operation"]==action["operation"] and stage["input"]==action
        before=stage["before"];handler=stage["after_handler"];after=stage["after"]
        _state(before,spec,position,roi,mode=mode,text=text,painted=painted,label=last_label)
        if previous is not None:
            assert before["image"]==previous["image"] and before["display"]==previous["display"] and before["roi"]==previous["roi"]
            assert before["raw_statistics"]==previous["raw_statistics"]
            if mode: assert before["contrast_adjuster"]["auto_threshold"]==previous["contrast_adjuster"]["auto_threshold"]
        elif spec["image_kind"]=="CompositeImage":
            assert before["display"]["default16bit_range"]==0
            for lut,pair in zip(before["display"]["channel_luts"],spec["ranges"]):
                _scalar(lut["min"],float(pair[0]));_scalar(lut["max"],float(pair[1]))
        else:
            assert before["display"]["default16bit_range"]==0
            pair=spec["ranges"][0] if spec["ranges"] else _reset_bounds(spec,position,0)
            _range(before["display"],_getter_bounds(spec,pair))
        op=action["operation"];decision=None;inherited_setup=False
        dispatch=stage["dispatch"]
        assert set(dispatch)=={"button_dispatch","subevents"}
        is_button=op in {"auto","reset","reset_then_12bit"}
        if op=="auto":
            decision=_auto(_raw_expected(spec,position,roi),before["contrast_adjuster"]["auto_threshold"])
            pair=_reset_bounds(spec,position,before["display"]["default16bit_range"]) if decision["reset"] else decision["bounds"]
        elif op in {"reset","reset_then_12bit"}: pair=_reset_bounds(spec,position,before["display"]["default16bit_range"])
        elif op=="numeric_set": pair=list(map(float,action["range"]))
        else: pair=None
        delta=_record_delta(mode,op,decision) if mode else ""
        if op=="owner_away_back" and mode=="Macro":
            # WindowManager:53--54 records actual logical image switches;
            # Recorder:216--232 suppresses these only in script mode.
            assert row["id"]=="H16" and group["other"] is not None
            delta='selectImage('+json.dumps(group["other"]["name"])+');\nselectImage('+json.dumps(spec["name"])+');\n'
        assert stage["recorder_before"]==text and stage["recorder_delta"]==delta
        text+=delta
        assert stage["recorder_after"]==text
        old_position=position.copy();old_roi=copy.deepcopy(roi)
        if op=="navigate": position=action["position_tcz"].copy()
        elif op=="set_roi": roi=copy.deepcopy(action["roi"])
        elif op=="restore_fixture": position=spec["position_tcz"].copy();roi=copy.deepcopy(spec["roi"])
        _state(handler,spec,position,roi,mode=mode,text=text,painted=painted,label=last_label,
               plot_current=is_button)
        if is_button:
            event=dispatch["button_dispatch"]
            _button(event,"auto" if op=="auto" else "reset",before["contrast_adjuster"],handler["contrast_adjuster"],before["image"]["id"])
            buttons.append(event)
            inherited_setup=handler["contrast_adjuster"]["new_images"]>before["contrast_adjuster"]["new_images"]
            if inherited_setup:
                assert spec["image_kind"]=="CompositeImage"
                assert handler["contrast_adjuster"]["setups"]>before["contrast_adjuster"]["setups"]
            expected_cycle=decision["divisor"] if op=="auto" and not inherited_setup else 0
            assert handler["contrast_adjuster"]["auto_threshold"]==expected_cycle
            controller_pair=_getter_bounds(spec,pair) if inherited_setup else pair
            _scalar(handler["contrast_adjuster"]["min"],float(controller_pair[0]));_scalar(handler["contrast_adjuster"]["max"],float(controller_pair[1]))
            reset_increment=int(op!="auto" or decision["reset"])
            assert handler["contrast_adjuster"]["resets"]==before["contrast_adjuster"]["resets"]+reset_increment
        else: assert dispatch["button_dispatch"] is None
        if pair is not None:
            _range(handler["display"],_getter_bounds(spec,pair))
            if spec["image_kind"]=="CompositeImage":
                for c,(old,lut) in enumerate(zip(before["display"]["channel_luts"],handler["display"]["channel_luts"])):
                    all_reset=spec["dtype"]=="uint16" and before["display"]["default16bit_range"]!=0 and (op!="auto" or decision["reset"])
                    all_shift=bool(action.get("shift")) and spec["mode"]=="composite"
                    if c==position[1] or all_reset or all_shift:
                        # updateAllChannelsAndDraw copies actual processor's
                        # rounded bounds to all stored channels.
                        want=_getter_bounds(spec,pair) if all_shift or inherited_setup else pair
                        _scalar(lut["min"],float(want[0]));_scalar(lut["max"],float(want[1]))
                    else: assert lut==old
        def subevent_state(event,fields,*,expected_pair=None,final=False):
            # These are actual observations immediately after the named API,
            # before the next subevent. They are not disposable extra keys.
            assert set(event)==set(fields)|{"after"}
            for key,value in fields.items(): assert event[key]==value
            value=event["after"]
            _state(value,spec,position,roi,mode=mode,text=text,painted=painted,label=last_label)
            assert value["image"]["id"]==before["image"]["id"]
            assert value["host"]["window_identity"]==before["host"]["window_identity"]
            controller=value["contrast_adjuster"];old=before["contrast_adjuster"]
            for key in ("identity","worker_calls","resets","assigned_sequence","completed_sequence"):
                assert controller[key]==old[key]
            assert controller["worker"]["identity"]==old["worker"]["identity"]
            if controller["new_images"]>old["new_images"]:
                assert spec["image_kind"]=="CompositeImage" and controller["setups"]>old["setups"]
                assert controller["auto_threshold"]==0
            else: assert controller["auto_threshold"]==old["auto_threshold"]
            if expected_pair is not None:
                _range(value["display"],_getter_bounds(spec,expected_pair))
                if spec["image_kind"]=="CompositeImage":
                    for c,(prior,lut) in enumerate(zip(before["display"]["channel_luts"],value["display"]["channel_luts"])):
                        if c==position[1]:
                            _scalar(lut["min"],float(expected_pair[0]));_scalar(lut["max"],float(expected_pair[1]))
                        else: assert lut==prior
            assert value["raw_statistics_call_count"]<handler["raw_statistics_call_count"]
            if final:
                for key in ("image","roi","display","raw_statistics","processor_after_raw_statistics","host","recorder","plot"):
                    assert value[key]==handler[key],key
                for key in controller:
                    if key!="worker": assert controller[key]==handler["contrast_adjuster"][key],key
            return value
        if op=="reset_then_12bit":
            assert [s["api"] for s in dispatch["subevents"]]==["ImagePlus.setDefault16bitRange","ImagePlus.setDefault16bitRange.return"]
            a,b=dispatch["subevents"];assert a["before"]==0 and a["requested"]==12 and b["actual"]==12
            assert a["state_before"]["display"]["default16bit_range"]==0 and b["state_after"]["display"]["default16bit_range"]==12
        elif is_button: assert dispatch["subevents"]==[]
        elif op=="set_roi":
            assert len(dispatch["subevents"])==1
            event=dispatch["subevents"][0];assert event["api"]=="ImagePlus.setRoi(Roi,false)" and event["assigned_is_retained"] is True
            _roi(event["assigned"],roi);assert event["assigned"]==event["retained"]
        elif op=="numeric_set":
            assert len(dispatch["subevents"])==2
            first,second=dispatch["subevents"]
            a=subevent_state(first,{"api":"ImagePlus.setDisplayRange","range":[0,200]},expected_pair=[0.,200.])
            b=subevent_state(second,{"api":"ImagePlus.updateAndDraw","explicit":True},expected_pair=[0.,200.],final=True)
            assert a["raw_statistics_call_count"]<b["raw_statistics_call_count"]
        elif op=="navigate":
            assert len(dispatch["subevents"])==(2 if action.get("redraw") else 1)
            a=subevent_state(dispatch["subevents"][0],{"api":"ImagePlus.setPosition","position_tcz":position},final=not action.get("redraw"))
            if action.get("redraw"):
                b=subevent_state(dispatch["subevents"][1],{"api":"ImagePlus.updateAndDraw","explicit":True},final=True)
                assert a["raw_statistics_call_count"]<b["raw_statistics_call_count"]
        elif op=="public_update":
            assert dispatch["subevents"]==[{"api":"ContrastAdjuster.update"}]
            assert handler["contrast_adjuster"]["new_images"]>before["contrast_adjuster"]["new_images"]
            assert handler["contrast_adjuster"]["auto_threshold"]==0
        elif op=="owner_away_back":
            assert len(dispatch["subevents"])==2
            away,back=dispatch["subevents"]
            assert away["api"]==back["api"]=="WindowManager.setCurrentWindow" and away["event_id"]==back["event_id"]==205
            event_keys={"api","source_image_id","event_id","event_identity","event_source_identity","event_dispatch_thread","opposite_window_identity","ca_after"}
            assert set(away)==set(back)==event_keys
            for event in (away,back):
                assert event["event_identity"]!=0 and event["event_source_identity"]==before["contrast_adjuster"]["identity"]
                assert event["event_dispatch_thread"] is True
                _ca(event["ca_after"])
                for key in ("identity","worker_calls","resets","assigned_sequence","completed_sequence"):
                    assert event["ca_after"][key]==before["contrast_adjuster"][key]
            assert away["event_identity"]!=back["event_identity"]
            # Both real dispatches target the same hidden CA window. In the
            # pinned JDK DefaultKeyboardFocusManager:531--536, the first
            # activation installs it; the second is consumed because that
            # window is already active. WindowManager's logical image switch
            # still occurs. Do not claim a second original handler invocation.
            for key in ("setups","setup_images","new_images"):
                assert away["ca_after"][key]==before["contrast_adjuster"][key]+1
            assert away["ca_after"]["previous_image_id"]==away["source_image_id"]
            assert away["ca_after"]["source_current_image_id"]==away["source_image_id"]
            assert back["ca_after"]=={**away["ca_after"],"source_current_image_id":back["source_image_id"]}
            for key in ("setups","setup_images","new_images","previous_image_id"):
                assert handler["contrast_adjuster"][key]==back["ca_after"][key]
            assert away["source_image_id"]!=before["image"]["id"] and back["source_image_id"]==before["image"]["id"]
            assert away["opposite_window_identity"]==before["host"]["window_identity"] and back["opposite_window_identity"]!=away["opposite_window_identity"]
            assert handler["contrast_adjuster"]["auto_threshold"]==0
        elif op=="restore_fixture":
            assert dispatch["subevents"][-1]=={"api":"restore_requested_fixture_display_no_cycle_write"}
            for lut,want in zip(handler["display"]["channel_luts"],spec["ranges"]):
                _scalar(lut["min"],float(want[0]));_scalar(lut["max"],float(want[1]))
            restored_pair=spec["ranges"][position[1]]
            _range(handler["display"],_getter_bounds(spec,restored_pair))
            processor=handler["processor_after_raw_statistics"]
            _scalar(processor["min"],float(restored_pair[0]));_scalar(processor["max"],float(restored_pair[1]))
            assert handler["display"]["visibility"]==spec["visibility"]+[True]*(8-len(spec["visibility"]))
            for key in ("auto_threshold","setups","setup_images","new_images","resets","worker_calls","assigned_sequence","completed_sequence"):
                assert handler["contrast_adjuster"][key]==before["contrast_adjuster"][key]
            # Inherited CompositeImage redraws may call CA.update. A zero cycle
            # remains an observation, never proof that restore preserved/reset it.
        elif op=="redraw":
            assert len(dispatch["subevents"])==1
            subevent_state(dispatch["subevents"][0],{"api":"ImagePlus.updateAndDraw","explicit":True},final=True)
        else: assert op=="statistics" and dispatch["subevents"]==[]
        if op=="statistics": assert stage["rgb"] is None
        else:
            painted+=1;last_label=group["name"]+"."+action["label"]
            if op=="restore_fixture" and spec["mode"] in {"color","grayscale"}:
                # CompositeImage.setMode/setChannelLut do not invalidate img;
                # Canvas.paint refreshes only imageUpdated or a null img. The
                # frozen restore has no redraw, so retain that observed cache
                # while still checking every restored logical field above.
                assert row["id"]=="H15" and previous is not None and index>0
                prior=row["stages"][offset+index-1]
                assert prior["context"]==group["name"] and prior["label"]=="auto.active"
                _rgb(stage["rgb"],spec,old_position,previous["display"])
                assert stage["rgb"]["rgb_hex"]==prior["rgb"]["rgb_hex"]
                assert stage["rgb"]["canvas_rgb_sha256"]==prior["rgb"]["canvas_rgb_sha256"]
                current_processor=after["processor_after_raw_statistics"]
                prior_processor=previous["processor_after_raw_statistics"]
                for key in ("identity","pixels_identity","source_pixels_identity","bits_hex"):
                    assert current_processor[key]==prior_processor[key]
            else: _rgb(stage["rgb"],spec,position,after["display"])
        _state(after,spec,position,roi,mode=mode,text=text,painted=painted,label=last_label)
        assert after["image"]["id"]==before["image"]["id"]
        assert after["display"]==handler["display"]
        if mode:
            a,b=handler["contrast_adjuster"],after["contrast_adjuster"]
            for key in ("worker_calls","resets","assigned_sequence","completed_sequence"): assert a[key]==b[key]
            if b["auto_threshold"]!=a["auto_threshold"]:
                assert b["auto_threshold"]==0 and b["new_images"]>a["new_images"]
        if group["compare_recorded_autos"] and op=="auto": comparisons.append((group,spec,stage,old_position,old_roi,decision))
        previous=after
    return position,roi,text,buttons,comparisons


def _comparisons(actual,expected):
    assert len(actual)==len(expected)
    for observation,(group,spec,stage,position,roi,decision) in zip(actual,expected):
        assert observation["context"]==group["name"] and observation["original_label"]==stage["label"]
        assert observation["recorded_delta"]==stage["recorder_delta"]
        assert observation["executed"] is (not decision["reset"])
        if decision["reset"]:
            assert set(observation)=={"context","original_label","recorded_delta","executed","reason"}
            assert observation["reason"]=="actual_Auto_delta_has_no_Enhance_Contrast_line"
            continue
        before,handler,after=(observation[k] for k in ("before","after_handler","after"))
        assert before["image"]["id"]!=stage["before"]["image"]["id"]
        _source(observation["source_before"],spec,roi);_source(observation["source_after"],spec,roi)
        assert observation["source_before"]==observation["source_after"]
        for state in (before,handler,after): _state(state,spec,position,roi)
        call=observation["dispatch"]
        assert call["api"]=="IJ.run(ImagePlus,String,String)" and call["command"]=="Enhance Contrast" and call["options"]=="saturated=0.35"
        assert call["image_id"]==call["bound_before_id"]==before["image"]["id"]
        _thread(call["thread"],edt=True);transport._finite(call["elapsed_ms"],5000)
        pair=_enhance(_raw_expected(spec,position,roi))
        assert pair is not None
        _range(handler["display"],_getter_bounds(spec,pair));assert handler["display"]==after["display"]
        if spec["image_kind"]=="CompositeImage":
            for c,(old,lut) in enumerate(zip(before["display"]["channel_luts"],handler["display"]["channel_luts"])):
                if c==position[1]:
                    _scalar(lut["min"],pair[0]);_scalar(lut["max"],pair[1])
                else: assert old==lut
        _rgb(observation["rgb"],spec,position,after["display"])
        assert before["host"]["offscreen_paint"]["count"]==0 and handler["host"]["offscreen_paint"]["count"]==0
        assert after["host"]["offscreen_paint"]["count"]==1 and after["host"]["offscreen_paint"]["last_label"]=="comparison."+stage["label"]
        _close_host(observation["cleanup"],spec)
        # The very purpose of these rows is to expose GUI Auto's different
        # per-bin rule. Their recorded commands are not treated as replay parity.
        assert pair!=decision["bounds"]


def _raw_calls(row,expected_buttons):
    calls=row["raw_statistics_calls"]
    assert [v["ordinal"] for v in calls]==list(range(1,len(calls)+1))
    assert calls
    # Snapshot getters are bound to the row's actual super-call ledger; each
    # counter identifies the final call made inside that exact snapshot.
    def snapshots(value):
        if isinstance(value,list):
            for cell in value: yield from snapshots(cell)
        elif isinstance(value,dict):
            if {"raw_statistics_call_count","raw_statistics","image"}<=set(value): yield value
            for cell in value.values(): yield from snapshots(cell)
    for state in snapshots({k:v for k,v in row.items() if k!="raw_statistics_calls"}):
        index=state["raw_statistics_call_count"]
        assert type(index) is int and 1<=index<=len(calls)
        actual=calls[index-1]
        assert actual["phase"]=="snapshot.getRawStatistics" and actual["image_id"]==state["image"]["id"]
        assert actual["statistics"]==state["raw_statistics"]["statistics"]
    candidates=[]
    for group in INPUTS[row["id"]]["groups"]:
        for spec in (group["fixture"],group["other"]):
            if spec is None: continue
            rois=[None,spec["roi"]]+[a["roi"] for a in group["actions"] if a["operation"]=="set_roi"]
            for t in range(spec["shape_tczyx"][0]):
                for c in range(spec["shape_tczyx"][1]):
                    for z in range(spec["shape_tczyx"][2]):
                        for roi in rois: candidates.append(_raw_expected(spec,[t,c,z],roi))
    auto_calls=[]
    for call in calls:
        assert call["image_id"]!=0 and call["image_identity"]!=0 and call["returned_statistics_identity"]!=0
        assert call["observer_class"] in {JAVA_CLASS+"$ObservedImagePlus",JAVA_CLASS+"$ObservedCompositeImage"}
        assert call["original_base_class"]==("ij.CompositeImage" if call["observer_class"].endswith("$ObservedCompositeImage") else "ij.ImagePlus")
        assert call["method_declaring_class"]=="ij.ImagePlus" and call["super_calls"]==1
        assert call["phase"] in {"snapshot.getRawStatistics","setup","setup.ImagePlus","setupNewImage","reset","autoAdjust","construction_or_inherited_call"}
        assert call["callers"] and any("ij.ImagePlus.getRawStatistics" in frame or "HistogramAutoReference$Observed" in frame for frame in call["callers"])
        _thread(call["thread"])
        # Each complete returned statistics object must match one independently
        # authored plane/ROI in this case, including source temporarily unmasked
        # by original setupNewImage. No producer expected output is consulted.
        matched=False
        for expected in candidates:
            try: _statistics(call["statistics"],expected)
            except AssertionError: continue
            matched=True;break
        assert matched,(row["id"],call["ordinal"],call["phase"])
        if call["phase"]=="autoAdjust": auto_calls.append(call)
    roi_at={};offset=0
    for group in INPUTS[row["id"]]["groups"]:
        roi=group["fixture"]["roi"]
        for action in group["actions"]:
            roi_at[offset]=roi
            if action["operation"]=="set_roi": roi=action["roi"]
            elif action["operation"]=="restore_fixture": roi=group["fixture"]["roi"]
            offset+=1
    buttons=[s for s in row["stages"] if s["operation"]=="auto"]
    assert len(auto_calls)==len(buttons)
    for stage in buttons:
        event=stage["dispatch"]["button_dispatch"]
        relevant=[c for c in auto_calls if c["event_sequence"]==event["sequence"]]
        assert len(relevant)==1
        call=relevant[0]
        assert call["image_id"]==stage["before"]["image"]["id"]
        assert call["thread"]["identity"]==event["worker_call"]["thread"]["identity"] and call["thread"]["event_dispatch_thread"] is False
        _statistics(call["statistics"],_raw_expected(row["fixtures"][stage["fixture"]],stage["before"]["image"]["position_tcz"],roi_at[stage["ordinal"]]))
    assert sum(len(g) for g in expected_buttons)==sum(s["operation"] in {"auto","reset","reset_then_12bit"} for s in row["stages"])


def _environment(actual,prepared_directory=None):
    assert actual["imagej_version"]=="1.54r" and actual["java_version"]=="21.0.7"
    assert actual["java_vendor"]=="Azul Systems, Inc." and actual["os_name"].startswith("Windows")
    assert actual["awt_headless"] is False and actual["event_dispatch_thread"] is True
    _scalar(actual["gui_scale"],1.);_hidden(actual["imagej"])
    assert actual["live_reference_hosts"]==0
    assert actual["scope"]=="hidden original ImageCanvas/ImageWindow or StackWindow; inherited ContrastAdjuster buttons and worker; offscreen paint; Recorder(false); no OS input or product acceptance"
    originals={"ij.ImageJ","ij.ImagePlus","ij.CompositeImage","ij.gui.ImageCanvas","ij.gui.ImageWindow","ij.gui.StackWindow",
        "ij.plugin.frame.ContrastAdjuster","ij.plugin.frame.ContrastPlot","ij.plugin.frame.Recorder","ij.process.ImageStatistics",
        "ij.process.ByteStatistics","ij.process.ShortStatistics","ij.process.FloatStatistics","ij.measure.Calibration","ij.gui.ShapeRoi",
        "ij.process.StackStatistics","ij.WindowManager"}
    observers={JAVA_CLASS,JAVA_CLASS+"$ObservedImagePlus",JAVA_CLASS+"$ObservedCompositeImage",JAVA_CLASS+"$ObservedAdjuster",JAVA_CLASS+"$ActualContrastPlot"}
    assert set(actual["loaded_classes"])==originals|observers
    from urllib.parse import unquote,urlparse
    def local(url):
        value=unquote(urlparse(url).path)
        if value.startswith("/") and len(value)>2 and value[2]==":": value=value[1:]
        return Path(value).resolve()
    observer_origins=set()
    for cls,value in actual["loaded_classes"].items():
        code=local(value["code_source"]);resource=local(value["resource"])
        if cls in originals: assert code==transport.REUSED_CLASSES.resolve()
        else: observer_origins.add(code)
        assert resource==code/(cls.replace(".","/")+".class")
    assert len(observer_origins)==1
    origin=observer_origins.pop()
    assert origin.is_relative_to(STAGE.resolve()) and origin.name=="classes"
    if prepared_directory is not None: assert origin==(prepared_directory/"classes").resolve()


def assert_case(row):
    case=row["id"];assert case in CASE_IDS and row["contract"]==CONTRACT
    spec=INPUTS[case]
    assert row["input_contract"]==spec
    fixtures={s["name"]:s for g in spec["groups"] for s in (g["fixture"],g["other"]) if s is not None}
    assert row["fixtures"]==fixtures and set(row["source_before"])==set(row["source_after"])==set(row["host_setup"])==set(fixtures)
    assert len(row["stages"])==STAGE_COUNTS[CASE_IDS.index(case)]
    transport._finite(row["elapsed_ms"],5000)
    offset=0;comparison_inputs=[];all_buttons=[]
    assert len(row["cleanup"])==len(spec["groups"])
    for group,cleanup in zip(spec["groups"],row["cleanup"]):
        source=group["fixture"]
        position,roi,text,buttons,comparisons=_group_stages(row,group,offset)
        offset+=len(group["actions"]);comparison_inputs+=comparisons;all_buttons.append(buttons)
        assert cleanup["context"]==group["name"]
        if group["statistics_only"]:
            assert cleanup["recorder_final"] is None and cleanup["contrast_adjuster"] is None
        else:
            _recorder(cleanup["recorder_final"],group["recorder_mode"],text)
            ca=cleanup["contrast_adjuster"]
            assert ca["joined"] is True and ca["after"]["done"] is True
            assert ca["after"]["worker"]["alive"] is False and ca["after"]["worker"]["state"]=="TERMINATED"
            _hidden(ca["after"]["dialog"])
            assert len(ca["events"])==len(buttons)
            assert [e["sequence"] for e in ca["events"]]==[b["sequence"] for b in buttons]
            pending=[c for c in ca["calls"] if c["pending"]]
            assert pending==[b["worker_call"] for b in buttons]
            assert all(c["error"] is None and c["super_returned"] is True for c in ca["calls"])
        assert len(cleanup["hosts"])==(2 if group["other"] else 1)
        for owner,closed in zip((source,group["other"]),cleanup["hosts"]):
            if owner is None: continue
            _source(row["source_before"][owner["name"]],owner,owner["roi"])
            _source(row["source_after"][owner["name"]],owner,roi if owner is source else owner["roi"])
            before=row["source_before"][owner["name"]];after=row["source_after"][owner["name"]]
            for key in ("shape_tczyx","bit_depth","planes","calibration","properties"): assert before[key]==after[key]
            setup=row["host_setup"][owner["name"]]
            assert len(setup["order"])==18 and setup["order"][0]=="construct_original_ImagePlus_or_CompositeImage"
            assigned=setup["roi_assignment"]
            assert assigned["api"]=="ImagePlus.setRoi(Roi,false)" and assigned["assigned_is_retained"] is True
            _roi(assigned["assigned"],owner["roi"]);assert assigned["assigned"]==assigned["retained"]
            _host(setup["configured"],owner,owner["position_tcz"],painted=0,label=None)
            assert setup["before_attachment"]["identity"]==setup["configured"]["image_identity"]==setup["after_attachment"]["image_identity"]
            _close_host(closed,owner)
    _comparisons(row["comparisons"],comparison_inputs)
    comparison_actions=[]
    for cleanup_index,group in enumerate(spec["groups"]):
        if group["comparison_action"] is not None:
            comparison_actions.append({"context":group["name"],"input":group["comparison_action"],
                "comparison_indices":[i for i,c in enumerate(row["comparisons"]) if c["context"]==group["name"]],
                "original_cleanup_index":cleanup_index,"after_all_original_contexts_closed":True})
    assert row["comparison_actions"]==comparison_actions
    _raw_calls(row,all_buttons);_environment(row["environment"])
    # Entry H16's fourteenth action is a post-timeline comparison action for
    # EACH Recorder context, represented here by all seven pre-Auto candidates.
    if case=="H16":
        assert [s["context"] for s in row["stages"]]==["Macro"]*13+["Java"]*13
        for context in ("Macro","Java"):
            comparisons=[c for c in row["comparisons"] if c["context"]==context]
            assert [c["original_label"] for c in comparisons]==["auto.initial","auto.after_roi","auto.after_set","auto.after_update","auto.after_navigation","auto.after_activation","auto.after_reset"]
            assert [c["original_label"] for c in comparisons if c["executed"]]==["auto.initial"]
    return row


def _raw(raw):
    assert len(raw)<=MAX_PAYLOAD
    rows=[];remaining=MAX_EXPANDED
    for line in raw.splitlines():
        if not line.strip(): continue
        encoded=transport._json(line)
        assert encoded.get("observation_encoding")=="observed_json_subtree_intern_v1"
        row=_decode_tables(encoded,logical_limit=remaining)
        remaining-=_json_size(row,remaining)
        rows.append(row)
    assert [r["id"] for r in rows]==list(CASE_IDS)
    assert sum(len(r["stages"]) for r in rows)==80
    assert sum(len(r["source_before"]) for r in rows)==28
    stages=[s for r in rows for s in r["stages"]]
    assert sum(s["operation"]=="auto" for s in stages)==51
    assert sum(s["operation"] in {"reset","reset_then_12bit"} for s in stages)==7
    assert sum(s["rgb"] is not None for s in stages)==78
    assert sum(len(r["comparisons"]) for r in rows)==15
    assert sum(len(r["comparison_actions"]) for r in rows)==3
    assert all(r["contract"]==CONTRACT for r in rows)
    return dict(zip(CASE_IDS,rows))


def _json_size(value,limit=MAX_EXPANDED):
    """Bound logical JSON before serializing any repeated expanded tree."""
    def size(cell,depth):
        assert depth<=128
        if isinstance(cell,dict):
            total=2+max(0,len(cell)-1)
            for key,item in cell.items():
                assert type(key) is str
                total+=len(json.dumps(key,ensure_ascii=True).encode("ascii"))+1+size(item,depth+1)
                assert total<=limit
            return total
        if isinstance(cell,list):
            total=2+max(0,len(cell)-1)
            for item in cell:
                total+=size(item,depth+1);assert total<=limit
            return total
        total=len(json.dumps(cell,ensure_ascii=True,allow_nan=False,separators=(",",":")).encode("ascii"))
        assert total<=limit
        return total
    result=size(value,0);assert result<=limit
    return result


def _decode_observations(encoded,logical_limit):
    assert encoded["observation_encoding"]=="observed_json_subtree_intern_v1"
    pool=encoded["observed_json_values"]
    assert type(pool) is list and len(pool)<=65536
    reserved={"observed_json_value","observation_encoding","observed_json_values"}
    def direct(value,depth=0):
        assert depth<=128
        if isinstance(value,dict):
            assert not(set(value)&reserved)
            for item in value.values(): direct(item,depth+1)
        elif isinstance(value,list):
            for item in value: direct(item,depth+1)
    # The identity/input timeline remains physically readable and cannot be
    # replaced by aliases. Only actual observation fields are pooled.
    for key in ("contract","id","input_contract","fixtures","environment","table_encoding","observed_lut_tables","comparison_actions"):
        direct(encoded[key])
    assert type(encoded["stages"]) is list and type(encoded["comparisons"]) is list
    for stage in encoded["stages"]:
        assert type(stage) is dict and not(set(stage)&reserved)
        for key in ("ordinal","label","context","fixture","operation","input"): direct(stage[key])
    for comparison in encoded["comparisons"]:
        assert type(comparison) is dict and not(set(comparison)&reserved)
        for key in ("context","original_label","executed","reason"):
            if key in comparison: direct(comparison[key])
    canonical=set();sizes=[];heights=[];edges=[]
    def describe(value,bound,depth=0):
        assert depth<=128
        if isinstance(value,dict):
            if "observed_json_value" in value:
                assert set(value)=={"observed_json_value"}
                index=value["observed_json_value"]
                assert type(index) is int and 0<=index<bound
                return sizes[index],heights[index],{index}
            assert not(set(value)&reserved)
            total=2+max(0,len(value)-1);height=0;references=set()
            for key,cell in value.items():
                assert type(key) is str
                count,child_height,child_refs=describe(cell,bound,depth+1)
                total+=len(json.dumps(key,ensure_ascii=True).encode("ascii"))+1+count
                height=max(height,child_height+1);references|=child_refs
                assert total<=logical_limit and height<=128
            return total,height,references
        if isinstance(value,list):
            total=2+max(0,len(value)-1);height=0;references=set()
            for cell in value:
                count,child_height,child_refs=describe(cell,bound,depth+1)
                total+=count;height=max(height,child_height+1);references|=child_refs
                assert total<=logical_limit and height<=128
            return total,height,references
        count=len(json.dumps(value,ensure_ascii=True,allow_nan=False,separators=(",",":")).encode("ascii"))
        assert count<=logical_limit
        return count,0,set()
    for index,value in enumerate(pool):
        assert type(value) in (str,dict,list)
        assert not(isinstance(value,dict) and "observed_json_value" in value)
        if isinstance(value,str): assert len(value.encode("utf-16-le"))//2>=24
        wire=json.dumps(value,sort_keys=True,ensure_ascii=True,allow_nan=False,separators=(",",":")).encode("ascii")
        assert wire not in canonical;canonical.add(wire)
        size,height,references=describe(value,index)
        sizes.append(size);heights.append(height);edges.append(references)
    body={key:value for key,value in encoded.items() if key not in {"observation_encoding","observed_json_values"}}
    _,_,roots=describe({key:value for key,value in body.items() if key not in {"table_encoding","observed_lut_tables"}},len(pool))
    used=set();pending=list(roots)
    while pending:
        index=pending.pop()
        if index not in used:
            used.add(index);pending.extend(edges[index])
    assert used==set(range(len(pool)))
    # Compute the SECOND layer's final logical weights on the encoded DAG too.
    # No recursive copies are materialized until the complete structure with
    # restored LUT strings has passed its final byte/depth bound.
    lut_pool=encoded["observed_lut_tables"]
    assert type(lut_pool) is list and len(lut_pool)<=256
    assert len(lut_pool)==len(set(lut_pool))
    for table in lut_pool:
        assert type(table) is str and len(table)==1536 and all(c in "0123456789abcdef" for c in table)
    final_sizes=[];final_heights=[]
    def final_weight(value,bound,depth=0,key=None):
        assert depth<=128
        if key in {"rgb_interleaved_hex","lut_rgb_interleaved_hex"} and value is not None:
            if isinstance(value,dict) and "observed_json_value" in value:
                assert set(value)=={"observed_json_value"}
                index=value["observed_json_value"]
                assert type(index) is int and 0<=index<bound
                return final_weight(pool[index],index,depth+1,key)
            assert type(value) is dict and set(value)=={"observed_lut_table"}
            index=value["observed_lut_table"]
            assert type(index) is int and 0<=index<len(lut_pool)
            return 1538,0
        if isinstance(value,dict):
            if "observed_json_value" in value:
                assert set(value)=={"observed_json_value"}
                index=value["observed_json_value"]
                assert type(index) is int and 0<=index<bound
                return final_sizes[index],final_heights[index]
            # A table reference has meaning only at its two declared keys.
            assert "observed_lut_table" not in value
            total=2+max(0,len(value)-1);height=0
            for field,cell in value.items():
                count,child_height=final_weight(cell,bound,depth+1,field)
                total+=len(json.dumps(field,ensure_ascii=True).encode("ascii"))+1+count
                height=max(height,child_height+1)
                assert total<=logical_limit and height<=128
            return total,height
        if isinstance(value,list):
            total=2+max(0,len(value)-1);height=0
            for cell in value:
                count,child_height=final_weight(cell,bound,depth+1)
                total+=count;height=max(height,child_height+1)
                assert total<=logical_limit and height<=128
            return total,height
        count=len(json.dumps(value,ensure_ascii=True,allow_nan=False,separators=(",",":")).encode("ascii"))
        assert count<=logical_limit
        return count,0
    for index,value in enumerate(pool):
        size,height=final_weight(value,index)
        final_sizes.append(size);final_heights.append(height)
    final_body={key:value for key,value in body.items() if key not in {"table_encoding","observed_lut_tables"}}
    final_weight(final_body,len(pool))
    expanded=[]
    def expand(value):
        if isinstance(value,dict):
            if "observed_json_value" in value: return expanded[value["observed_json_value"]]
            return {key:expand(cell) for key,cell in value.items()}
        if isinstance(value,list): return [expand(cell) for cell in value]
        return value
    for value in pool: expanded.append(expand(value))
    return expand(body)


def _decode_tables(encoded,*,logical_limit=MAX_EXPANDED):
    """Strict lossless expansion of actual observed LUTs, never expectations."""
    if "observation_encoding" in encoded:
        encoded=_decode_observations(encoded,logical_limit)
    else:
        # The dedicated environment probe intentionally retains LUT-only
        # transport. Required H rows are checked above and cannot use it.
        assert encoded.get("kind")=="environment_probe_no_H_cases" and "id" not in encoded
        assert "observed_json_values" not in encoded
    assert encoded["table_encoding"]=="observed_rgb_hex_intern_v1"
    pool=encoded["observed_lut_tables"]
    assert isinstance(pool,list) and len(pool)<=256
    assert len(pool)==len(set(pool))
    for table in pool:
        assert type(table) is str and len(table)==1536 and all(c in "0123456789abcdef" for c in table)
        assert len(bytes.fromhex(table))==768
    used=set()
    def measured(value,depth=0):
        assert depth<=128
        if isinstance(value,dict):
            assert "observed_lut_table" not in value
            total=2+max(0,len(value)-1)
            for key,cell in value.items():
                total+=len(json.dumps(key,ensure_ascii=True).encode("ascii"))+1
                if key in {"rgb_interleaved_hex","lut_rgb_interleaved_hex"} and cell is not None:
                    assert type(cell) is dict and set(cell)=={"observed_lut_table"}
                    index=cell["observed_lut_table"]
                    assert type(index) is int and 0<=index<len(pool)
                    used.add(index);total+=1538  #quoted exact lowercase hex
                else: total+=measured(cell,depth+1)
                assert total<=logical_limit
            return total
        if isinstance(value,list):
            total=2+max(0,len(value)-1)
            for cell in value:
                total+=measured(cell,depth+1);assert total<=logical_limit
            return total
        size=len(json.dumps(value,ensure_ascii=True,allow_nan=False,separators=(",",":")).encode("ascii"))
        assert size<=logical_limit
        return size
    def expand(value):
        if isinstance(value,list): return [expand(v) for v in value]
        if not isinstance(value,dict): return value
        assert "observed_lut_table" not in value  #references only at the two declared keys
        result={}
        for key,cell in value.items():
            if key in {"rgb_interleaved_hex","lut_rgb_interleaved_hex"} and cell is not None:
                assert type(cell) is dict and set(cell)=={"observed_lut_table"}
                index=cell["observed_lut_table"]
                assert type(index) is int and 0<=index<len(pool)
                used.add(index);result[key]=pool[index]
            else: result[key]=expand(cell)
        return result
    body={k:v for k,v in encoded.items() if k not in {"table_encoding","observed_lut_tables"}}
    measured(body)
    assert used==set(range(len(pool)))
    decoded=expand(body)
    assert used==set(range(len(pool)))
    _json_size(decoded,logical_limit)
    return decoded


def _default_raw():
    return Path(os.environ.get("IMAGEJXX_HISTOGRAM_AUTO_REFERENCE",STAGE/"capture-001/reference.stdout")).resolve()


def _pin(value,limit=MAX_PAYLOAD):
    return transport._pin(value,limit)


def _json_file(path):
    return transport._json(transport._bytes(path,MAX_PAYLOAD))


def _archive_identity(verified):
    expected={"version":"1.54r","commit":transport.COMMIT,"source_archive_sha256":transport.ARCHIVE_SHA,
              "runtime_archive_sha256":transport.RUNTIME_SHA}
    assert verified["reference"]==expected
    assert [(v["bytes"],v["sha256"]) for v in verified["archives"]]==[(6146010,transport.ARCHIVE_SHA),(705099776,transport.RUNTIME_SHA)]
    _pin(verified["archives"][0],MAX_BUILD)
    old_raw=_pin(verified["reused_receipt"])
    assert hashlib.sha256(old_raw).hexdigest()==transport.RECEIPT_SHA
    old=transport._json(old_raw);assert old["reference"]==expected
    for key,original in (("source_tree","verified_source_tree"),("runtime_tree","verified_runtime_tree"),("reused_classes","compiled_classes")):
        assert verified[key]==transport._manifest(old[original])
    for name in ("bin/java.exe","bin/javac.exe"):
        expected=old["verified_runtime_tree"][name]
        content=transport._bytes(ROOT/"build/references/roi-style-imagej154r/jdk"/name,MAX_BUILD)
        assert len(content)==expected["bytes"] and hashlib.sha256(content).hexdigest()==expected["sha256"]
    for name in ("ij/ImageJ.class","ij/ImagePlus.class","ij/CompositeImage.class","ij/gui/ImageCanvas.class","ij/gui/ImageWindow.class","ij/gui/StackWindow.class",
        "ij/plugin/frame/ContrastAdjuster.class","ij/plugin/frame/ContrastPlot.class","ij/plugin/frame/Recorder.class","ij/process/ImageStatistics.class",
        "ij/process/ByteStatistics.class","ij/process/ShortStatistics.class","ij/process/FloatStatistics.class","ij/measure/Calibration.class","ij/gui/ShapeRoi.class",
        "ij/process/StackStatistics.class","ij/WindowManager.class"):
        content=transport._bytes(transport.REUSED_CLASSES/name,MAX_BUILD);expected=old["compiled_classes"][name]
        assert len(content)==expected["bytes"] and hashlib.sha256(content).hexdigest()==expected["sha256"]
    for name,value in verified["known_source_members"].items():
        assert value==old["verified_source_tree"][name]
    base=transport._json(_pin(BASE_ENTRY))
    sources={v["relative_path"]:{"bytes":v["bytes"],"sha256":v["sha256"]} for v in base["reference"]["source_members"]}
    extras={"ij/gui/ShapeRoi.java":"8ab4a507fed1ddd09391c5663d482350c9f035e17788d915b440915277fc419e",
        "ij/process/PolygonFiller.java":"2d6c43bb6fed5bf206fa552bf0f60883117436884fcd9869ec4b9474ef7c5968",
        "ij/WindowManager.java":"68dc7efb78b8c7fc446b5649a61ad8787366af6afc38f8fe62ae36ca5dddc2bf",
        "ij/process/StackStatistics.java":"78f1c311eaf156dfced948c2d8f5d0489a71629951b528dd4bb106961277cdb9"}
    for name,sha in extras.items():
        sources[name]=old["verified_source_tree"][name]
        assert sources[name]["sha256"]==sha
    assert verified["histogram_auto_source_members"]==sources
    # These immutable archive-derived copies are the behavioral sources actually
    # inspected for the independent model, not a newly fetched moving source.
    for name,pin in sources.items():
        path=ROOT/"build/references/binary-imagej154r/sources"/name
        content=transport._bytes(path,MAX_PAYLOAD)
        assert len(content)==pin["bytes"] and hashlib.sha256(content).hexdigest()==pin["sha256"]
    assert verified["histogram_auto_entries"]==list(ENTRIES)
    for entry in ENTRIES: _pin(entry)
    lineage=verified["qualified_supervisor_source"]
    assert lineage["path"]=="scripts/capture_composite_min_reference.py" and lineage["sha256"]=="84e355ed4a509c1e95ae63f8736cddf3f23a68c6ce2bad0bbe91e0fb37e8f16a"
    _pin(lineage)


def _command_file(stage,label,process,parent):
    command=_json_file(stage/(label+".command.json"))
    assert command["argv"]==process["argv"] and command["timeout_seconds"]==process["timeout_seconds"]
    transport._finite(command["parent_remaining_seconds"],parent)
    assert command["scope"]=="reference observer only; no OS input or product acceptance"


def _probe(probe,plan,prepared_directory):
    assert probe["contract"]==CONTRACT and probe["kind"]=="environment_probe_no_H_cases"
    transport._finite(probe["elapsed_ms"],5000)
    fixture=plan["probe_fixture"]
    expected=_fixture("environment_probe",shape=(2,2,2,3,5),position=(1,1,1),ranges=[[0,31],[2,47]])
    expected["tables"]=["red","green"];expected["visibility"]=[True,False]
    assert fixture==expected
    assert fixture not in [s for row in INPUTS.values() for g in row["groups"] for s in (g["fixture"],g["other"]) if s is not None]
    before,auto,reset=probe["before"],probe["auto_after"],probe["reset_after"]
    _state(before,fixture,[1,1,1],None,mode="Macro",text="",painted=0,label=None)
    decision=_auto(_raw_expected(fixture,[1,1,1],None),before["contrast_adjuster"]["auto_threshold"])
    assert decision["reset"] is False  #15 samples reach actual Auto record path
    _button(probe["auto_dispatch"],"auto",before["contrast_adjuster"],auto["contrast_adjuster"],before["image"]["id"])
    _button(probe["reset_dispatch"],"reset",auto["contrast_adjuster"],reset["contrast_adjuster"],before["image"]["id"])
    assert probe["cleanup"]["contrast_adjuster"]["joined"] is True
    assert probe["cleanup"]["contrast_adjuster"]["after"]["worker"]["alive"] is False
    for mode,recorder in probe["recorder_modes"].items():
        assert mode in {"Macro","Java"}
        _recorder(recorder,mode,recorder["text"])
    assert set(probe["recorder_modes"])=={"Macro","Java"}
    assert "Enhance Contrast" in probe["recorder_modes"]["Macro"]["text"]
    assert "resetMinAndMax" in probe["recorder_modes"]["Macro"]["text"]
    assert probe["recorder_modes"]["Java"]["text"]==""
    _rgb(probe["paint"],fixture,[1,1,1],reset["display"])
    _close_host(probe["cleanup"]["host"],fixture)
    _environment(probe["environment"],prepared_directory)


def _load(raw_path):
    # Only pure JSON/path/hash transport is imported from the qualified reader.
    # Pin that source explicitly; numerical models and H inputs are owned here.
    _pin(TRANSPORT_READER)
    raw_path=Path(raw_path).resolve()
    assert raw_path.is_relative_to(STAGE.resolve()) and raw_path.name=="reference.stdout"
    receipt=_json_file(raw_path.parent/"capture.json")
    assert receipt["contract"]==CONTRACT and receipt["state"]=="raw_observations_only_no_numerical_or_product_acceptance"
    assert receipt["ordered_case_ids"]==list(CASE_IDS) and receipt["case_count"]==16 and receipt["stage_count"]==80
    assert receipt["policy_decisions"]==[]
    transport._finite(receipt["elapsed_seconds"],120)
    raw=_pin(receipt["raw"]);assert (ROOT/receipt["raw"]["path"]).resolve()==raw_path
    prepared=transport._json(_pin(receipt["prepared"]))
    prepared_directory=(ROOT/receipt["prepared"]["path"]).parent
    assert prepared_directory.is_relative_to(STAGE.resolve())
    assert prepared["contract"]==CONTRACT and prepared["state"]=="prepared_environment_only_H01_H16_not_executed"
    assert prepared["harness"]==receipt["harness"]
    assert prepared["harness"]["path"]=="scripts/capture_histogram_auto_reference.py" and prepared["harness"]["sha256"]==PRODUCER_SHA
    _pin(prepared["harness"])
    plan=transport._json(_pin(prepared["input_plan"]))
    assert plan["contract"]==CONTRACT and plan["ordered_case_ids"]==list(CASE_IDS) and plan["cases"]==list(INPUTS.values())
    canonical=json.dumps(list(INPUTS.values()),sort_keys=True,separators=(",",":"),allow_nan=False).encode()
    assert plan["cases_canonical_sha256"]==hashlib.sha256(canonical).hexdigest()
    assert plan["bounds"]==BOUNDS and plan["deadlines_ms"]=={"idle":500,"event_completion":1000,"worker_join":500}
    assert plan["table_encoding"]=="observed_rgb_hex_intern_v1"
    assert plan["observation_encoding"]=="observed_json_subtree_intern_v1"
    pooling=plan["observation_pool"]
    assert pooling["eligibility"]=={"string_min_utf16_units":24,"container_min_ascii_json_bytes":96}
    assert pooling["guards"]=={"max_depth":128,"max_pool_entries_per_row":65536,
        "max_fully_expanded_json_bytes_combined":MAX_EXPANDED}
    assert pooling["reserved_keys"]==["observed_json_value","observation_encoding","observed_json_values"]
    assert pooling["direct_roots"]==["contract","id","input_contract","fixtures","environment","table_encoding","observed_lut_tables","comparison_actions"]
    assert pooling["direct_stage_fields"]==["ordinal","label","context","fixture","operation","input"]
    assert pooling["direct_comparison_fields"]==["context","original_label","executed","reason"]
    env=plan["environment_inputs"]
    assert env["ImageJ_constructor"]=="ImageJ.NO_SHOW" and env["gui_scale"]==1
    assert env["auto_contrast"] is False and env["use_inverting_lut"] is False
    assert env["magnification"]==2 and env["paint_method"]==PAINT_METHOD
    assert env["image_observers"]=={"ordinary":JAVA_CLASS+"$ObservedImagePlus","composite":JAVA_CLASS+"$ObservedCompositeImage",
        "method":"getRawStatistics; super once; return identical object; clone actual fields/counts with phase/caller/thread/sequence"}
    assert plan["ordered_stage_labels"]=={case:[g["name"]+"."+a["label"] for g in spec["groups"] for a in g["actions"]] for case,spec in INPUTS.items()}
    assert plan["ordered_comparison_action_labels"]=={case:[g["name"]+"."+g["comparison_action"]["label"] for g in spec["groups"] if g["comparison_action"]] for case,spec in INPUTS.items()}
    membership=plan["output_membership"]
    for key,value in {"raw_jsonl_rows":16,"original_stages":80,"source_before_after_pairs":28,"real_button_dispatches":58,
        "Auto_button_dispatches":51,"Reset_button_dispatches":7,"comparison_actions":3,"total_declared_actions_including_comparisons":83,
        "pre_Auto_comparison_candidates":15,"per_case_stages":list(STAGE_COUNTS)}.items(): assert membership[key]==value
    assert membership["RGB"]=="one per non-statistics original stage and actually executed comparison; statistics-only stages have null RGB"
    assert "no OS input" in plan["scope"] and "no OS input" in receipt["scope"]
    verified=transport._json(_pin(prepared["verified_inputs"]))
    _archive_identity(verified);assert plan["entry_contracts"]==verified["histogram_auto_entries"]
    execution=transport._json(_pin(prepared["execution_inputs"]))
    for key in ("input_plan","verified_inputs","driver"): assert execution[key]==prepared[key]
    assert execution["harness"]==prepared["harness"]
    driver=_pin(prepared["driver"])
    assert driver.startswith(b"package ij.plugin.frame;") and JAVA_CLASS.split(".")[-1].encode() in driver
    tools={pin["path"].split("/")[-1]:_pin(pin) for pin in execution["tools"]}
    assert set(tools)=={"capture_histogram_auto_reference.py","capture_composite_min_reference.py","capture_roi_boolean_reference.py","capture_binary_reference.py"}
    assert hashlib.sha256(tools["capture_histogram_auto_reference.py"]).hexdigest()==PRODUCER_SHA
    assert hashlib.sha256(tools["capture_composite_min_reference.py"]).hexdigest()=="84e355ed4a509c1e95ae63f8736cddf3f23a68c6ce2bad0bbe91e0fb37e8f16a"
    names={"HistogramAutoReference","HistogramAutoReference$ObservedImagePlus","HistogramAutoReference$ObservedCompositeImage",
        "HistogramAutoReference$HiddenImageWindow","HistogramAutoReference$HiddenStackWindow","HistogramAutoReference$Host",
        "HistogramAutoReference$ActualContrastPlot","HistogramAutoReference$ObservedAdjuster","HistogramAutoReference$Clone","HistogramAutoReference$Replay",
        "HistogramAutoReference$ObservationPool"}
    assert set(prepared["prepared_classes"])=={"ij/plugin/frame/"+name+".class" for name in names}
    for name,pin in prepared["prepared_classes"].items():
        relative=PurePosixPath(name);assert relative.as_posix()==name and not relative.is_absolute() and ".." not in relative.parts
        content=transport._bytes(prepared_directory/"classes"/name,MAX_BUILD)
        assert len(content)==pin["bytes"] and hashlib.sha256(content).hexdigest()==pin["sha256"]
    for name,maximum,label in (("compile",60,"javac"),("version",10,"version"),("probe",15,"probe")):
        process=prepared["processes"][name]
        assert process["exit_code"]==0 and 1<=process["timeout_seconds"]<=maximum
        _pin(process["stdout"]);_pin(process["stderr"])
        _command_file(prepared_directory,label,process,120)
    assert transport._json(_pin(prepared["processes"]["probe"]["stdout"]))==prepared["probe"]
    _probe(_decode_tables(prepared["probe"]),plan,prepared_directory)
    consumed=transport._json(_pin(receipt["execution_inputs"]))
    assert consumed["prepared"]==receipt["prepared"] and consumed["harness"]==prepared["harness"]
    for key in ("input_plan","verified_inputs","driver"): assert consumed[key]==prepared[key]
    assert consumed["ordered_case_ids"]==list(CASE_IDS) and consumed["bounds"]==BOUNDS
    assert transport._bytes(raw_path.parent/"input-plan.json",MAX_PAYLOAD)==_pin(prepared["input_plan"])
    process=receipt["process"]
    assert process["exit_code"]==0 and 1<=process["timeout_seconds"]<=90
    assert process["stdout"]==receipt["raw"] and _pin(process["stderr"])==b""
    assert process["argv"][-2:]==[JAVA_CLASS,"--capture"] and "-Djava.awt.headless=false" in process["argv"] and "-Xmx256m" in process["argv"]
    assert Path(process["argv"][0]).resolve()==(ROOT/"build/references/roi-style-imagej154r/jdk/bin/java.exe").resolve()
    command=_json_file(raw_path.parent/"command.json")
    assert command["argv"]==process["argv"] and command["maximum_process_seconds"]==90 and command["parent_seconds"]==120
    assert command["ordered_case_ids"]==list(CASE_IDS) and command["prepared"]==receipt["prepared"]
    assert _pin(command["input_plan"])==_pin(prepared["input_plan"])
    _command_file(raw_path.parent,"reference",process,120)
    # Decode case pools only after the encoded report and every consumed
    # receipt/tool/source/runtime/plan/class identity above have been verified.
    observed=_raw(raw)
    for row in observed.values(): _environment(row["environment"],prepared_directory)
    for directory in (raw_path.parent,prepared_directory):
        files=[p for p in directory.rglob("*") if p.is_file()]
        assert all(not p.is_symlink() for p in files)
        assert sum(p.stat().st_size for p in files)<=MAX_BUILD
        assert sum(p.stat().st_size for p in files if p.suffix in {".json",".stdout",".stderr"})<=MAX_PAYLOAD
    return observed


@pytest.fixture(scope="module")
def reference_cases():
    return _load(_default_raw())


def assert_raw(raw):
    """Validate raw rows without claiming their archive/provenance envelope."""
    content=raw if isinstance(raw,bytes) else transport._bytes(Path(raw),MAX_PAYLOAD)
    rows=_raw(content)
    for row in rows.values(): assert_case(row)
    return rows


def assert_capture(path=None):
    rows=_load(path if path is not None else _default_raw())
    for row in rows.values(): assert_case(row)
    return rows


@pytest.mark.parametrize("case_id",CASE_IDS)
def test_histogram_auto_reference(case_id,reference_cases):
    assert_case(reference_cases[case_id])

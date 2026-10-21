"""Bounded, separately prepared ImageJ 1.54r Histogram/GUI Auto observations.

Only this new observer is compiled. ImageJ classes, sources and the JDK are
verified through the existing reference supervisor; no shared cache is changed.
Prepare exercises a dedicated environment probe, never an H case. Capture is
an explicit second command. Outputs are observations, not parity decisions.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import time
import traceback

sys.dont_write_bytecode = True
import capture_composite_min_reference as lineage

reference = lineage.reference
ROOT = reference.ROOT
STAGE = ROOT / "build/references/histogram-auto-imagej154r-001"
CONTRACT = "g057.histogram-auto-reference.001"
CLASS_NAME = "ij.plugin.frame.HistogramAutoReference"
CASE_IDS = tuple(f"H{i:02d}" for i in range(1, 17))
ENTRIES = {
    "docs/parity/reviews/G033-histogram-auto-entry-001.json": "2f511fb6dcfc1f26c62060681eded31b6e21b025de37cd123777405b9d280c8e",
    "docs/parity/reviews/G033-histogram-auto-entry-002.json": "b623c9c42c7059a0d1eba6919a5917170fba00bdfd216c8621dbcca0e94f64b9",
    "docs/parity/reviews/G033-histogram-auto-entry-003.json": "4ebbf78efbab889d23c07221b2af07664a24952d679ba0e2df253aa8c9782f67",
}
LINEAGE_SHA = "84e355ed4a509c1e95ae63f8736cddf3f23a68c6ce2bad0bbe91e0fb37e8f16a"
BOUNDS = {"case_count": 16, "case_seconds": 5, "prepare_parent_seconds": 120,
          "capture_parent_seconds": 120, "max_xy_pixels": 4096,
          "max_scalar_samples_per_case": 65536, "java_heap_mib": 256,
          "combined_payload_bytes": 4194304, "fresh_stage_bytes": 33554432,
          "max_live_reference_hosts": 2}
EXTRA_SOURCE_PINS = {
    "ij/gui/ShapeRoi.java": "8ab4a507fed1ddd09391c5663d482350c9f035e17788d915b440915277fc419e",
    "ij/process/PolygonFiller.java": "2d6c43bb6fed5bf206fa552bf0f60883117436884fcd9869ec4b9474ef7c5968",
    "ij/WindowManager.java": "68dc7efb78b8c7fc446b5649a61ad8787366af6afc38f8fe62ae36ca5dddc2bf",
    "ij/process/StackStatistics.java": "78f1c311eaf156dfced948c2d8f5d0489a71629951b528dd4bb106961277cdb9",
}
PAINT_METHOD = "ImageCanvas.paint(Graphics2D) on private BufferedImage.TYPE_INT_RGB"
HOST_ORDER = ["construct_original_ImagePlus_or_CompositeImage", "observe_before_attachment",
              "construct_original_ImageCanvas", "construct_hidden_ImageWindow_or_StackWindow",
              "bind_WindowManager_current_window_and_EDT_temporary_image", "observe_after_attachment",
              "apply_declared_calibration_and_display", "setPosition_through_bound_window",
              "assign_declared_ROI", "set_canvas_source_rect", "set_canvas_drawing_size",
              "set_canvas_magnification_last", "observe_configured", "construct_ContrastAdjuster",
              "run_original_ContrastAdjuster", "await_original_worker_WAITING", "construct_Recorder_false",
              "dispatch_real_Recorder_mode_ItemEvent"]


def fixture(name, dtype="uint16", shape=(2, 3, 2, 6, 8), *, values=None,
            position=(1, 2, 1), composite=True, roi=None, calibration="none",
            mode="composite", projection=None, ranges=None, flags=None, offset=0, spatial=False):
    """Input bytes only. No histogram, display range or Auto expectation."""
    t_count, c_count, z_count, height, width = shape
    planes = []
    for t in range(t_count):
        for z in range(z_count):
            for c in range(c_count):
                samples = (values if values is not None else
                           [40*t+30*c+5*z+2*y+x+offset for y in range(height) for x in range(width)])
                if len(samples) != width*height:
                    raise ValueError("Fixture scalar count differs")
                if dtype == "float32":
                    raw = b"".join(struct.pack("<I", v) if type(v) is int else struct.pack("<f", v) for v in samples)
                else:
                    raw = b"".join(struct.pack("<B" if dtype == "uint8" else "<H", v) for v in samples)
                planes.append({"t": t, "c": c, "z": z, "bits_hex": raw.hex()})
    return {"name": name, "dtype": dtype, "shape_tczyx": list(shape), "position_tcz": list(position),
            "image_kind": "CompositeImage" if composite else "ImagePlus", "planes": planes,
            "roi": roi, "intensity_calibration": calibration, "spatial_calibration":
            ({"spacing_xyz": [.5, 1.25, 2.5], "pixel_origin_xyz": [11, -7, 19],
             "unit": "um", "frame_interval": .75, "time_unit": "s"} if spatial else None),
            "mode": mode, "projection": projection,
            "ranges": ranges or ([[0, 255]]*c_count if dtype == "uint8" else ([[1, 201], [7, 219], [11, 241]] if composite else None)),
            "tables": ["red", "green", "blue"][:c_count] if composite else None,
            "visibility": flags if flags is not None else ([True, False, True] if composite else [True]*c_count)}


def action(label, operation=None, **kwargs):
    return {"label": label, "operation": operation or ("reset" if label == "reset" else "auto"), **kwargs}


def group(name, spec, actions, *, recorder="Macro", other=None, comparisons=False, statistics_only=False, comparison_label=None):
    return {"name": name, "fixture": spec, "other": other, "recorder_mode": recorder,
            "actions": actions, "compare_recorded_autos": comparisons, "statistics_only": statistics_only, "comparison_action": (action(comparison_label, "recorded_comparisons") if comparisons else None)}


def cases():
    ordinary = dict(shape=(1, 1, 1, 10, 10), position=(0, 0, 0), composite=False)
    u8 = lambda name, values, **kw: fixture(name, "uint8", values=values, **ordinary, **kw)
    common_ranges = [[1, 201], [7, 219], [11, 241]]
    rows = []
    def add(number, groups):
        rows.append({"id": f"H{number:02d}", "groups": groups})
    h01 = [10]+[20]*3+[30]*200+[40]*200+[50]*200+[60]*3492
    add(1, [group("main", fixture("main", "uint8", (1, 1, 1, 64, 64), values=h01,
             position=(0, 0, 0), composite=False), [action(x) for x in ("auto.1", "auto.2", "auto.3", "reset", "auto.after_reset")])])
    distribution = [1]+[2]*10+[3]*11+[4]*78
    add(2, [group("main", u8("main", distribution), [action("auto")], comparisons=True, comparison_label="recorded_enhance_contrast.saturated_0_35")])
    add(3, [group("main", u8("main", [7]*100), [action("auto"), action("auto.repeated")])])
    add(4, [group("main", u8("main", [10]*8+[200]*92), [action("auto")])])
    for number, dtype, values in ((5, "uint16", [1000+257*i for i in range(16)]),
                                  (6, "float32", [-2.5+.5*i for i in range(16)])):
        add(number, [group("main", fixture("main", dtype, (1, 1, 1, 1, 16), values=values,
              position=(0, 0, 0), composite=False), [action("auto"), action("redraw", "redraw")])])
    add(7, [group("main", fixture("main", roi={"kind": "rectangle", "xywh": [1, 1, 6, 4]}),
              [action("auto"), action("navigate_t0c0z0", "navigate", position_tcz=[0, 0, 0]), action("auto.after_navigation")])])
    add(8, [group("main", fixture("main", roi={"kind": "holed_area", "outer_xywh": [2, 1, 4, 4], "hole_xywh": [3, 2, 2, 2]}), [action("auto")])])
    add(9, [group("main", fixture("main", roi={"kind": "point", "xy": [2, 2]}),
              [action("auto.point"), action("reset"), action("set_line_roi", "set_roi", roi={"kind": "line", "xyxy": [1, 1, 6, 4]}), action("auto.line")])])
    add(10, [group("main", fixture("main", roi={"kind": "empty_area", "anchor": [2, 1], "constructor": "new ShapeRoi(2,1,new java.awt.geom.Area())"}), [action("auto")])])
    bits = [0x7fc12345, 0xffc23456, 0x80000000, 0]+[struct.unpack("<I", struct.pack("<f", x))[0] for x in (-5, -4, -3, -2, -1, 1, 2, 3, 4, 5, 6, 7)]
    add(11, [group("main", fixture("main", "float32", (1, 1, 1, 1, 16), values=bits,
             position=(0, 0, 0), composite=False), [action("auto"), action("redraw", "redraw")])])
    add(12, [group("main", fixture("main", "float32", (1, 1, 1, 1, 16), values=[0x7fc12345]*16,
             position=(0, 0, 0), composite=False), [action("statistics", "statistics"), action("auto")]),
             group("mixed_statistics_only", fixture("mixed_statistics_only", "float32", (1, 1, 1, 1, 4),
             values=[0x7fc12345, 0xff800000, 0x7f800000, 0x40e00000], position=(0, 0, 0), composite=False),
             [action("statistics", "statistics")], statistics_only=True)])
    h13 = []
    for name, composite, cal in (("ordinary_unsigned", False, "none"), ("composite_unsigned", True, "none"), ("ordinary_signed16", False, "signed16")):
        spec = (fixture(name, shape=(2, 3, 2, 10, 10), values=[7]*100, position=(1, 1, 1), ranges=common_ranges, flags=[True, False, True]) if composite else
                fixture(name, shape=(1, 1, 1, 10, 10), values=[7]*100, position=(0, 0, 0), composite=False, calibration=cal))
        h13.append(group(name, spec, [action("auto.automatic"), action("reset_cycle_and_set_12bit", "reset_then_12bit"), action("auto.12bit")]))
    add(13, h13)
    add(14, [group(name, fixture(name, calibration=cal, spatial=True), [action("auto.raw" if cal == "none" else "auto.intensity_calibrated")])
             for name, cal in (("raw", "none"), ("linear", "linear"), ("signed16", "signed16"))])
    add(15, [group(name, fixture(name, position=(1, 1, 1), ranges=common_ranges, flags=[True, False, True], mode=mode, projection=projection),
                   [action("auto.active", shift=False), action("restore_fixture", "restore_fixture"), action("auto.shift_all", shift=True)])
             for name, mode, projection in (("composite_sum", "composite", None), ("composite_max", "composite", "Max"),
                   ("composite_min", "composite", "Min"), ("color_max", "color", "Max"), ("gray_max", "grayscale", "Max"))])
    h16 = []
    for context in ("Macro", "Java"):
        spec = fixture(context+".original", "uint8", (2, 3, 2, 10, 10), position=(1, 1, 1), flags=[True, False, True])
        other = fixture(context+".other", "uint8", (2, 3, 2, 10, 10), position=(1, 1, 1), flags=[True, False, True])
        for target, extra in ((spec, 0), (other, 40)):
            for plane in target["planes"]:
                plane["bits_hex"] = bytes(v+5*plane["t"]+10*plane["c"]+2*plane["z"]+extra for v in distribution).hex()
        timeline = [action("auto.initial"), action("change_area_roi", "set_roi", roi={"kind": "rectangle", "xywh": [1, 1, 8, 8]}),
                    action("auto.after_roi"), action("set_numeric_range", "numeric_set", range=[0, 200], redraw=True),
                    action("auto.after_set"), action("public_update", "public_update"), action("auto.after_update"),
                    action("navigate_czt", "navigate", position_tcz=[0, 2, 0], redraw=True), action("auto.after_navigation"),
                    action("owner_away_back", "owner_away_back"), action("auto.after_activation"), action("reset"), action("auto.after_reset")]
        h16.append(group(context, spec, timeline, recorder=context, other=other, comparisons=True, comparison_label="recorded_enhance_contrast_on_fresh_owner"))
    add(16, h16)
    if tuple(row["id"] for row in rows) != CASE_IDS:
        raise ValueError("Exact H01--H16 membership differs")
    for row in rows:
        count = sum(sum(len(bytes.fromhex(p["bits_hex"]))//{"uint8": 1, "uint16": 2, "float32": 4}[s["dtype"]] for p in s["planes"])
                    for g in row["groups"] for s in (g["fixture"], g["other"]) if s is not None)
        if count > BOUNDS["max_scalar_samples_per_case"]:
            raise ValueError("Input case exceeds scalar bound")
    return rows


DRIVER = r'''package ij.plugin.frame;
import ij.*;
import ij.gui.*;
import ij.measure.Calibration;
import ij.process.*;
import java.awt.*;
import java.awt.event.*;
import java.awt.geom.Area;
import java.awt.image.*;
import java.lang.reflect.*;
import java.nio.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.List;
import java.util.concurrent.*;

public final class HistogramAutoReference {
 static final String CONTRACT="g057.histogram-auto-reference.001";
 static final List<Object> CASES=__CASES__;
 static final List<Object> HOST_ORDER=__HOST_ORDER__;
 static ImageJ app; static ObservedAdjuster ca; static Recorder recorder; static Choice recorderChoice;
 static Object recorderModeDispatch; static long deadline; static long nextSequence;
 static final Map<ImagePlus,Host> HOSTS=new IdentityHashMap<>();
 static final ThreadLocal<String> PHASE=new ThreadLocal<>();static final List<Object> RAW_CALLS=new ArrayList<>();static long rawSequence;
 static String phase(String next){String previous=PHASE.get();PHASE.set(next);return previous;}
 static void recordRaw(ImagePlus image,ImageStatistics result,String base){List<Object> callers=new ArrayList<>();for(StackTraceElement frame:Thread.currentThread().getStackTrace())if(frame.getClassName().startsWith("ij."))callers.add(frame.toString());synchronized(RAW_CALLS){RAW_CALLS.add(o("ordinal",++rawSequence,"image_id",image.getID(),"image_identity",id(image),"observer_class",image.getClass().getName(),"original_base_class",base,"method_declaring_class","ij.ImagePlus","returned_statistics_identity",id(result),"super_calls",1,"phase",PHASE.get()==null?"construction_or_inherited_call":PHASE.get(),"event_sequence",ca==null?0:ca.assigned,"thread",thread(Thread.currentThread()),"callers",callers,"statistics",stats(result)));}}
 static final class ObservedImagePlus extends ImagePlus {ObservedImagePlus(String title,ImageStack stack){super(title,stack);}@Override public ImageStatistics getRawStatistics(){ImageStatistics actual=super.getRawStatistics();recordRaw(this,actual,"ij.ImagePlus");return actual;}}
 static final class ObservedCompositeImage extends CompositeImage {ObservedCompositeImage(ImagePlus source,int mode){super(source,mode);}@Override public ImageStatistics getRawStatistics(){ImageStatistics actual=super.getRawStatistics();recordRaw(this,actual,"ij.CompositeImage");return actual;}}

 static Map<String,Object> o(Object...p){Map<String,Object> m=new LinkedHashMap<>();for(int i=0;i<p.length;i+=2)m.put((String)p[i],p[i+1]);return m;}
 static List<Object> a(Object...v){return new ArrayList<>(Arrays.asList(v));}
 @SuppressWarnings("unchecked") static Map<String,Object> m(Object v){return (Map<String,Object>)v;}
 @SuppressWarnings("unchecked") static List<Object> l(Object v){return (List<Object>)v;}
 static int n(Object v){return ((Number)v).intValue();} static double d(Object v){return ((Number)v).doubleValue();}
 static int id(Object v){return v==null?0:System.identityHashCode(v);}
 static String quote(String s){StringBuilder b=new StringBuilder("\"");for(int i=0;i<s.length();i++){char c=s.charAt(i);if(c=='"'||c=='\\')b.append('\\').append(c);else if(c<32||c>127)b.append(String.format("\\u%04x",(int)c));else b.append(c);}return b.append('"').toString();}
 static String json(Object v){if(v==null)return "null";if(v instanceof String)return quote((String)v);if(v instanceof Number){if(v instanceof Double&&!Double.isFinite((Double)v))throw new IllegalArgumentException("Unencoded nonfinite scalar");return v.toString();}if(v instanceof Boolean)return v.toString();StringBuilder b=new StringBuilder();if(v instanceof Map){b.append('{');boolean first=true;for(Object raw:((Map<?,?>)v).entrySet()){Map.Entry<?,?>e=(Map.Entry<?,?>)raw;if(!first)b.append(',');first=false;b.append(quote((String)e.getKey())).append(':').append(json(e.getValue()));}return b.append('}').toString();}b.append('[');boolean first=true;if(v instanceof Iterable){for(Object x:(Iterable<?>)v){if(!first)b.append(',');first=false;b.append(json(x));}}else if(v.getClass().isArray()){for(int i=0;i<Array.getLength(v);i++){if(i>0)b.append(',');b.append(json(Array.get(v,i)));}}else throw new IllegalArgumentException("Unknown JSON observation "+v.getClass());return b.append(']').toString();}
 static Object internLuts(Object value,String key,List<Object> pool,Map<String,Integer> indices){if(("rgb_interleaved_hex".equals(key)||"lut_rgb_interleaved_hex".equals(key))&&value!=null){if(!(value instanceof String)||!((String)value).matches("[0-9a-f]{1536}"))throw new IllegalStateException("Observed LUT is not an exact256x3byte table");String raw=(String)value;Integer index=indices.get(raw);if(index==null){index=pool.size();indices.put(raw,index);pool.add(raw);}return o("observed_lut_table",index);}if(value instanceof Map){Map<String,Object>out=new LinkedHashMap<>();for(Map.Entry<String,Object>entry:m(value).entrySet())out.put(entry.getKey(),internLuts(entry.getValue(),entry.getKey(),pool,indices));return out;}if(value instanceof List){List<Object>out=new ArrayList<>();for(Object item:l(value))out.add(internLuts(item,null,pool,indices));return out;}return value;}
 static Object encodeTables(Object row){List<Object>pool=new ArrayList<>();Map<String,Object>encoded=m(internLuts(row,null,pool,new LinkedHashMap<>()));encoded.put("table_encoding","observed_rgb_hex_intern_v1");encoded.put("observed_lut_tables",pool);return encoded;}
 // Lossless transport only. Children are interned before parents, so every
 // reference in pool entry i points strictly below i. Input/identity envelopes
 // remain direct; expansion restores the complete pre-pool observation tree.
 static final Set<String> DIRECT_ROOT=new HashSet<>(Arrays.asList("contract","id","input_contract","fixtures","environment","table_encoding","observed_lut_tables","comparison_actions"));
 static final Set<String> DIRECT_STAGE=new HashSet<>(Arrays.asList("ordinal","label","context","fixture","operation","input"));
 static final Set<String> DIRECT_COMPARISON=new HashSet<>(Arrays.asList("context","original_label","executed","reason"));
 static String canonicalJson(Object value){if(value instanceof Map){Map<String,Object> sorted=new TreeMap<>(m(value));StringBuilder out=new StringBuilder("{");boolean first=true;for(Map.Entry<String,Object> entry:sorted.entrySet()){if(!first)out.append(',');first=false;out.append(quote(entry.getKey())).append(':').append(canonicalJson(entry.getValue()));}return out.append('}').toString();}if(value instanceof List){StringBuilder out=new StringBuilder("[");boolean first=true;for(Object item:l(value)){if(!first)out.append(',');first=false;out.append(canonicalJson(item));}return out.append(']').toString();}return json(value);}
 static void rejectObservationKeys(Object value,int depth){if(depth>128)throw new IllegalStateException("Observation exceeds transport depth128");if(value instanceof Map){Map<String,Object> map=m(value);if(map.containsKey("observed_json_value")||map.containsKey("observation_encoding")||map.containsKey("observed_json_values"))throw new IllegalStateException("Actual observation collides with reserved transport key");for(Object child:map.values())rejectObservationKeys(child,depth+1);}else if(value instanceof Iterable){for(Object child:(Iterable<?>)value)rejectObservationKeys(child,depth+1);}else if(value!=null&&value.getClass().isArray()){for(int i=0;i<Array.getLength(value);i++)rejectObservationKeys(Array.get(value,i),depth+1);}}
 static final class ObservationPool {
  final List<Object> values=new ArrayList<>();final Map<String,Integer> indices=new HashMap<>();
  Object intern(Object value,String key){Integer index=indices.get(key);if(index==null){if(values.size()>=65536)throw new IllegalStateException("Observation pool exceeds65536entries");index=values.size();indices.put(key,index);values.add(value);}return o("observed_json_value",index);}
  Object walk(Object value,int depth){if(depth>128)throw new IllegalStateException("Observation exceeds transport depth128");if(value instanceof String)return ((String)value).length()>=24?intern(value,canonicalJson(value)):value;Object encoded;if(value instanceof Map){Map<String,Object> out=new LinkedHashMap<>();for(Map.Entry<String,Object> entry:m(value).entrySet())out.put(entry.getKey(),walk(entry.getValue(),depth+1));encoded=out;}else if(value instanceof Iterable){List<Object> out=new ArrayList<>();for(Object item:(Iterable<?>)value)out.add(walk(item,depth+1));encoded=out;}else if(value!=null&&value.getClass().isArray()){List<Object> out=new ArrayList<>();for(int i=0;i<Array.getLength(value);i++)out.add(walk(Array.get(value,i),depth+1));encoded=out;}else return value;String key=canonicalJson(encoded);return key.length()>=96?intern(encoded,key):encoded;}
  Object envelope(Object value,Set<String> direct){Map<String,Object> out=new LinkedHashMap<>();for(Map.Entry<String,Object> entry:m(value).entrySet())out.put(entry.getKey(),direct.contains(entry.getKey())?entry.getValue():walk(entry.getValue(),1));return out;}
 }
 static Object encodeObservations(Object row){rejectObservationKeys(row,0);ObservationPool pool=new ObservationPool();Map<String,Object> out=new LinkedHashMap<>();for(Map.Entry<String,Object> entry:m(row).entrySet()){String key=entry.getKey();Object value=entry.getValue();if(DIRECT_ROOT.contains(key))out.put(key,value);else if(key.equals("stages")||key.equals("comparisons")){List<Object> envelopes=new ArrayList<>();for(Object item:l(value))envelopes.add(pool.envelope(item,key.equals("stages")?DIRECT_STAGE:DIRECT_COMPARISON));out.put(key,envelopes);}else out.put(key,pool.walk(value,1));}out.put("observation_encoding","observed_json_subtree_intern_v1");out.put("observed_json_values",pool.values);return out;}
 static final char[] HEX="0123456789abcdef".toCharArray();
 static String hex(byte[] bytes){char[] chars=new char[bytes.length*2];for(int i=0;i<bytes.length;i++){chars[2*i]=HEX[(bytes[i]&255)>>>4];chars[2*i+1]=HEX[bytes[i]&15];}return new String(chars);}
 static byte[] bytes(String s){byte[] b=new byte[s.length()/2];for(int i=0;i<b.length;i++)b[i]=(byte)Integer.parseInt(s.substring(2*i,2*i+2),16);return b;}
 static String sha(byte[] b)throws Exception{return hex(MessageDigest.getInstance("SHA-256").digest(b));}
 static String shaUnchecked(byte[] b){try{return sha(b);}catch(Exception e){throw new IllegalStateException(e);}}
 static Object number(double value){byte[] b=ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN).putLong(Double.doubleToRawLongBits(value)).array();return o("value",Double.isFinite(value)?value:null,"classification",Double.isNaN(value)?"nan":value==Double.POSITIVE_INFINITY?"positive_infinity":value==Double.NEGATIVE_INFINITY?"negative_infinity":"finite","ieee754_f64_le_hex",hex(b));}
 static Object numbersFloats(float[] values){if(values==null)return null;List<Object>out=new ArrayList<>();for(float v:values)out.add(number(v));return out;}
 static Object numbers(double[] values){if(values==null)return null;List<Object> out=new ArrayList<>();for(double x:values)out.add(number(x));return out;}
 static Object field(Class<?> cls,Object owner,String name){try{Field f=cls.getDeclaredField(name);f.setAccessible(true);return f.get(owner);}catch(Exception e){throw new IllegalStateException("Cannot observe "+cls.getName()+"."+name,e);}}
 static Object rect(Rectangle r){return r==null?null:a(r.x,r.y,r.width,r.height);}
 static Object flags(Component c){return o("visible",c.isVisible(),"showing",c.isShowing(),"displayable",c.isDisplayable(),"enabled",c.isEnabled());}
 static Object thread(Thread t){return t==null?null:o("identity",id(t),"id",t.getId(),"name",t.getName(),"state",t.getState().toString(),"alive",t.isAlive(),"event_dispatch_thread",t==Thread.currentThread()&&EventQueue.isDispatchThread(),"observer_event_dispatch_thread",EventQueue.isDispatchThread(),"current_thread_identity",id(Thread.currentThread()));}
 static long remaining(){long value=deadline-System.nanoTime();if(value<=0)throw new IllegalStateException("Five-second case deadline exceeded");return value;}
 static long localDeadline(int millis){return Math.min(deadline,System.nanoTime()+TimeUnit.MILLISECONDS.toNanos(millis));}
 static <T>T edt(Callable<T> action)throws Exception{if(EventQueue.isDispatchThread())return action.call();FutureTask<T> task=new FutureTask<>(action);EventQueue.invokeLater(task);return task.get(remaining(),TimeUnit.NANOSECONDS);}
 static String scalarBits(Object p){int count=Array.getLength(p),size=p instanceof byte[]?1:p instanceof short[]?2:4;ByteBuffer b=ByteBuffer.allocate(count*size).order(ByteOrder.LITTLE_ENDIAN);for(int i=0;i<count;i++){if(size==1)b.put(((byte[])p)[i]);else if(size==2)b.putShort(((short[])p)[i]);else b.putInt(Float.floatToRawIntBits(((float[])p)[i]));}return hex(b.array());}
 static String storageBits(Object p){ByteBuffer b=ByteBuffer.allocate(Array.getLength(p)*8).order(ByteOrder.LITTLE_ENDIAN);for(int i=0;i<Array.getLength(p);i++){double v=p instanceof byte[]?((byte[])p)[i]&255:p instanceof short[]?((short[])p)[i]&65535:((float[])p)[i];b.putLong(Double.doubleToRawLongBits(v));}return hex(b.array());}
 static ImageProcessor processor(String type,int w,int h,String raw){byte[] b=bytes(raw);ByteBuffer data=ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN);if(type.equals("uint8"))return new ByteProcessor(w,h,b,null);if(type.equals("uint16")){short[]p=new short[w*h];for(int i=0;i<p.length;i++)p[i]=data.getShort();return new ShortProcessor(w,h,p,null);}float[]p=new float[w*h];for(int i=0;i<p.length;i++)p[i]=Float.intBitsToFloat(data.getInt());return new FloatProcessor(w,h,p,null);}
 static Object sparse(int[] values){if(values==null)return null;List<Object> cells=new ArrayList<>();for(int i=0;i<values.length;i++)if(values[i]!=0)cells.add(a(i,values[i]));return o("encoding","int32_nonzero_pairs","length",values.length,"nonzero",cells);}
 static Object stats(ImageStatistics s){if(s==null)return null;return o("class",s.getClass().getName(),"histogram",s.histogram==null?null:s.histogram.clone(),"histogram16",sparse(s.histogram16),"pixel_count",s.pixelCount,"long_pixel_count",s.longPixelCount,"min",number(s.min),"max",number(s.max),"hist_min",number(s.histMin),"hist_max",number(s.histMax),"bin_size",number(s.binSize),"mean",number(s.mean),"std_dev",number(s.stdDev),"mode",s.mode,"dmode",number(s.dmode),"area",number(s.area),"max_count",s.maxCount);}
 static Object rawStats(ImagePlus image){String old=phase("snapshot.getRawStatistics");try{return o("statistics",stats(image.getRawStatistics()),"error",null);}catch(Throwable e){return o("statistics",null,"error",error(e));}finally{phase(old);}}
 static Object error(Throwable e){return o("class",e.getClass().getName(),"message",e.getMessage());}
 static String table(IndexColorModel cm){if(cm==null)return null;byte[] b=new byte[cm.getMapSize()*3];for(int i=0;i<cm.getMapSize();i++){b[i*3]=(byte)cm.getRed(i);b[i*3+1]=(byte)cm.getGreen(i);b[i*3+2]=(byte)cm.getBlue(i);}return hex(b);}
 static Object mask(ImageProcessor p){return p==null?null:o("identity",id(p),"class",p.getClass().getName(),"shape",a(p.getHeight(),p.getWidth()),"bits_hex",scalarBits(p.getPixels()));}
 static Object roi(Roi r){if(r==null)return null;ImageProcessor one=r.getMask(),two=r.getMask();ij.process.FloatPolygon f=r.getFloatPolygon();List<Object> points=new ArrayList<>();if(f!=null)for(int i=0;i<f.npoints;i++)points.add(a(number(f.xpoints[i]),number(f.ypoints[i])));return o("identity",id(r),"class",r.getClass().getName(),"shape_array",r instanceof ShapeRoi?numbersFloats(((ShapeRoi)r).getShapeAsArray()):null,"type",r.getType(),"type_name",r.getTypeAsString(),"is_area",r.isArea(),"is_line",r.isLine(),"bounds_xywh",rect(r.getBounds()),"float_polygon_xy",points,"position",r.getPosition(),"czt_position",a(r.getCPosition(),r.getZPosition(),r.getTPosition()),"mask_first",mask(one),"mask_second",mask(two),"masks_same_identity",one==two,"shape_empty",r instanceof ShapeRoi?new Area(((ShapeRoi)r).getShape()).isEmpty():null);}
 static Object proc(ImagePlus image){ImageProcessor p=image.getProcessor();java.awt.image.ColorModel cm=p.getColorModel();return o("identity",id(p),"class",p.getClass().getName(),"pixels_identity",id(p.getPixels()),"source_pixels_identity",id(image.getStack().getPixels(image.getCurrentSlice())),"pixels_are_source",p.getPixels()==image.getStack().getPixels(image.getCurrentSlice()),"bits_hex",scalarBits(p.getPixels()),"min",number(p.getMin()),"max",number(p.getMax()),"min_threshold",number(p.getMinThreshold()),"max_threshold",number(p.getMaxThreshold()),"lut_update_mode",p.getLutUpdateMode(),"roi_xywh",rect(p.getRoi()),"mask",mask(p.getMask()),"lut_rgb_interleaved_hex",cm instanceof IndexColorModel?table((IndexColorModel)cm):null,"inverted_lut",p.isInvertedLut(),"calibration_table",p.getCalibrationTable()==null?null:o("length",p.getCalibrationTable().length,"bits_sha256",shaUnchecked(bytes(scalarBits(p.getCalibrationTable())))));}
 static Object calibration(ImagePlus image){Calibration c=image.getCalibration();return o("spacing_xyz",numbers(new double[]{c.pixelWidth,c.pixelHeight,c.pixelDepth}),"pixel_origin_xyz",numbers(new double[]{c.xOrigin,c.yOrigin,c.zOrigin}),"unit",c.getUnit(),"x_unit",c.getXUnit(),"y_unit",c.getYUnit(),"z_unit",c.getZUnit(),"frame_interval",number(c.frameInterval),"time_unit",c.getTimeUnit(),"value_unit",c.getValueUnit(),"function",c.getFunction(),"coefficients",numbers(c.getCoefficients()),"signed16",c.isSigned16Bit(),"calibrated",c.calibrated());}
 static Object properties(ImagePlus image){Properties p=image.getProperties();Map<String,Object> out=new TreeMap<>();if(p!=null)for(String key:p.stringPropertyNames())out.put(key,p.getProperty(key));return out;}
 static Object source(ImagePlus image){List<Object> planes=new ArrayList<>();for(int t=0;t<image.getNFrames();t++)for(int z=0;z<image.getNSlices();z++)for(int c=0;c<image.getNChannels();c++){int index=image.getStackIndex(c+1,z+1,t+1);Object p=image.getStack().getPixels(index);planes.add(o("t",t,"c",c,"z",z,"stack_index",index,"bits_hex",scalarBits(p),"storage_f64_hex",storageBits(p)));}return o("shape_tczyx",a(image.getNFrames(),image.getNChannels(),image.getNSlices(),image.getHeight(),image.getWidth()),"bit_depth",image.getBitDepth(),"planes",planes,"calibration",calibration(image),"properties",properties(image),"roi",roi(image.getRoi()));}
 static final class HiddenImageWindow extends ImageWindow {HiddenImageWindow(ImagePlus i,ImageCanvas c){super(i,c);}@Override public void show(){}@Override public void setVisible(boolean v){super.setVisible(false);}}
 static final class HiddenStackWindow extends StackWindow {HiddenStackWindow(ImagePlus i,ImageCanvas c){super(i,c);}@Override public void show(){}@Override public void setVisible(boolean v){super.setVisible(false);}}
 static final class Host {final ImagePlus image;final ImageCanvas canvas;final ImageWindow window;final BufferedImage surface;int paints;String lastLabel;Host(ImagePlus i){if(HOSTS.size()>=2)throw new IllegalStateException("More than two live reference hosts");image=i;canvas=new ImageCanvas(i);window=i.getStackSize()>1?new HiddenStackWindow(i,canvas):new HiddenImageWindow(i,canvas);surface=new BufferedImage(i.getWidth()*2,i.getHeight()*2,BufferedImage.TYPE_INT_RGB);}}
 static void bind(ImagePlus image){Host h=HOSTS.get(image);WindowManager.setCurrentWindow(h.window);WindowManager.setTempCurrentImage(image);}
 static Object selector(Host h,String name){if(!(h.window instanceof StackWindow))return null;ScrollbarWithLabel s=(ScrollbarWithLabel)field(StackWindow.class,h.window,name);return s==null?null:s.getValue()-1;}
 static Object host(ImagePlus image){Host h=HOSTS.get(image);if(h==null)return null;if(image.getWindow()!=h.window||image.getCanvas()!=h.canvas||h.canvas.getImage()!=image||h.window.getImagePlus()!=image||h.window.getCanvas()!=h.canvas)throw new IllegalStateException("Reference host identity changed");return o("image_id",image.getID(),"image_identity",id(image),"window_identity",id(h.window),"window_class",h.window.getClass().getName(),"canvas_identity",id(h.canvas),"canvas_class",h.canvas.getClass().getName(),"window_image_identity",id(h.window.getImagePlus()),"window_canvas_identity",id(h.window.getCanvas()),"canvas_image_identity",id(h.canvas.getImage()),"canvas_parent_identity",id(h.canvas.getParent()),"current_window_is_host",WindowManager.getCurrentWindow()==h.window,"current_image_is_source",WindowManager.getCurrentImage()==image,"position_tcz",a(image.getFrame()-1,image.getChannel()-1,image.getSlice()-1),"current_slice_index",image.getCurrentSlice()-1,"selector_position_czt",a(selector(h,"cSelector"),selector(h,"zSelector"),selector(h,"tSelector")),"open_as_hyperstack",image.getOpenAsHyperStack(),"displayed_hyperstack",image.isDisplayedHyperStack(),"window_hyperstack",h.window instanceof StackWindow?((StackWindow)h.window).isHyperStack():false,"window",flags(h.window),"canvas",o("flags",flags(h.canvas),"magnification",number(h.canvas.getMagnification()),"source_rect_xywh",rect(h.canvas.getSrcRect()),"drawing_wh",a(h.canvas.getSize().width,h.canvas.getSize().height)),"offscreen_paint",o("count",h.paints,"last_label",h.lastLabel,"method","ImageCanvas.paint(Graphics2D) on private BufferedImage.TYPE_INT_RGB","surface_wh",a(h.surface.getWidth(),h.surface.getHeight())));}
 static void hidden(){for(Window w:Window.getWindows())if(w.isVisible()||w.isShowing())throw new IllegalStateException("A reference top-level became visible: "+w.getClass().getName());}
 static Object paint(ImagePlus image,String label)throws Exception{Host h=HOSTS.get(image);Graphics2D g=h.surface.createGraphics();try{h.canvas.paint(g);}finally{g.dispose();}h.paints++;h.lastLabel=label;int w=image.getWidth(),hh=image.getHeight();int[] rgb=new int[w*hh];Image rendered=image.getImage();PixelGrabber grab=new PixelGrabber(rendered,0,0,w,hh,rgb,0,w);if(!grab.grabPixels(Math.max(1,TimeUnit.NANOSECONDS.toMillis(remaining()))))throw new IllegalStateException("Image RGB readback did not complete");byte[] bytes=new byte[rgb.length*3];for(int i=0;i<rgb.length;i++){bytes[i*3]=(byte)(rgb[i]>>>16);bytes[i*3+1]=(byte)(rgb[i]>>>8);bytes[i*3+2]=(byte)rgb[i];}hidden();return o("shape",a(hh,w,3),"rgb_hex",hex(bytes),"canvas_rgb_sha256",sha(ints(h.surface.getRGB(0,0,h.surface.getWidth(),h.surface.getHeight(),null,0,h.surface.getWidth()))));}
 static byte[] ints(int[] values){ByteBuffer b=ByteBuffer.allocate(values.length*4).order(ByteOrder.LITTLE_ENDIAN);for(int x:values)b.putInt(x);return b.array();}
 static Roi requestedRoi(Map<String,Object> spec){String k=(String)spec.get("kind");if(k.equals("rectangle")){List<Object>x=l(spec.get("xywh"));return new Roi(n(x.get(0)),n(x.get(1)),n(x.get(2)),n(x.get(3)));}if(k.equals("point")){List<Object>x=l(spec.get("xy"));return new PointRoi(n(x.get(0)),n(x.get(1)));}if(k.equals("line")){List<Object>x=l(spec.get("xyxy"));return new Line(n(x.get(0)),n(x.get(1)),n(x.get(2)),n(x.get(3)));}if(k.equals("empty_area"))return new ShapeRoi(2,1,new Area());if(k.equals("holed_area")){List<Object>x=l(spec.get("outer_xywh")),y=l(spec.get("hole_xywh"));ShapeRoi area=new ShapeRoi(new Roi(n(x.get(0)),n(x.get(1)),n(x.get(2)),n(x.get(3))));return area.not(new ShapeRoi(new Roi(n(y.get(0)),n(y.get(1)),n(y.get(2)),n(y.get(3)))));}throw new IllegalArgumentException("Unknown ROI fixture");}
 static Object assignRoi(ImagePlus image,Object spec){Roi assigned=spec==null?null:requestedRoi(m(spec));image.setRoi(assigned,false);return o("api","ImagePlus.setRoi(Roi,false)","assigned",roi(assigned),"retained",roi(image.getRoi()),"assigned_is_retained",assigned==image.getRoi());}
 static void configure(ImagePlus image,Map<String,Object> spec){Calibration cal=new Calibration();if(spec.get("spatial_calibration")!=null){Map<String,Object> c=m(spec.get("spatial_calibration"));List<Object>s=l(c.get("spacing_xyz")),p=l(c.get("pixel_origin_xyz"));cal.pixelWidth=d(s.get(0));cal.pixelHeight=d(s.get(1));cal.pixelDepth=d(s.get(2));cal.xOrigin=d(p.get(0));cal.yOrigin=d(p.get(1));cal.zOrigin=d(p.get(2));cal.setUnit((String)c.get("unit"));cal.frameInterval=d(c.get("frame_interval"));cal.setTimeUnit((String)c.get("time_unit"));}String intensity=(String)spec.get("intensity_calibration");if(intensity.equals("linear"))cal.setFunction(Calibration.STRAIGHT_LINE,new double[]{5,2},"a.u.");if(intensity.equals("signed16"))cal.setSigned16BitCalibration();image.setCalibration(cal);if(image instanceof CompositeImage){CompositeImage ci=(CompositeImage)image;List<Object>ranges=spec.get("ranges")==null?null:l(spec.get("ranges"));for(int i=0;i<image.getNChannels();i++){LUT lut=LUT.createLutFromColor(i==0?Color.red:i==1?Color.green:Color.blue);if(ranges!=null){List<Object>r=l(ranges.get(i));lut.min=d(r.get(0));lut.max=d(r.get(1));}else{LUT old=ci.getChannelLut(i+1);lut.min=old.min;lut.max=old.max;}ci.setChannelLut(lut,i+1);}if(spec.get("projection")!=null)ci.setProp("CompositeProjection",(String)spec.get("projection"));else ci.setProp("CompositeProjection",null);String mode=(String)spec.get("mode");ci.setMode(mode.equals("composite")?CompositeImage.COMPOSITE:mode.equals("color")?CompositeImage.COLOR:CompositeImage.GRAYSCALE);boolean[] flags=ci.getActiveChannels();List<Object>v=l(spec.get("visibility"));for(int i=0;i<v.size();i++)flags[i]=(Boolean)v.get(i);}List<Object>position=l(spec.get("position_tcz"));image.setPosition(n(position.get(1))+1,n(position.get(2))+1,n(position.get(0))+1);if(!(image instanceof CompositeImage)&&spec.get("ranges")!=null){List<Object>r=l(l(spec.get("ranges")).get(0));image.setDisplayRange(d(r.get(0)),d(r.get(1)));}}
 static ImagePlus build(Map<String,Object> spec,Map<String,Object> setups){List<Object>shape=l(spec.get("shape_tczyx"));int w=n(shape.get(4)),h=n(shape.get(3));ImageStack stack=new ImageStack(w,h);for(Object p:l(spec.get("planes")))stack.addSlice(processor((String)spec.get("dtype"),w,h,(String)m(p).get("bits_hex")));ImagePlus original=new ObservedImagePlus((String)spec.get("name"),stack);original.setDimensions(n(shape.get(1)),n(shape.get(2)),n(shape.get(0)));ImagePlus image=spec.get("image_kind").equals("CompositeImage")?new ObservedCompositeImage(original,CompositeImage.COMPOSITE):original;image.setOpenAsHyperStack(image.getStackSize()>1);Object before=imageInfo(image);Host host=new Host(image);HOSTS.put(image,host);bind(image);Object attached=host(image);configure(image,spec);Object assigned=assignRoi(image,spec.get("roi"));host.canvas.setSourceRect(new Rectangle(0,0,w,h));host.canvas.setDrawingSize(w*2,h*2);host.canvas.setMagnification(2);hidden();setups.put((String)spec.get("name"),o("order",HOST_ORDER,"before_attachment",before,"after_attachment",attached,"roi_assignment",assigned,"configured",host(image)));return image;}
 static Object imageInfo(ImagePlus image){return o("id",image.getID(),"identity",id(image),"class",image.getClass().getName(),"title",image.getTitle(),"shape_tczyx",a(image.getNFrames(),image.getNChannels(),image.getNSlices(),image.getHeight(),image.getWidth()),"position_tcz",a(image.getFrame()-1,image.getChannel()-1,image.getSlice()-1),"current_slice_index",image.getCurrentSlice()-1,"type",image.getType(),"bit_depth",image.getBitDepth(),"changes",image.changes,"overlay_count",image.getOverlay()==null?0:image.getOverlay().size(),"calibration",calibration(image),"properties",properties(image));}
 static Object display(ImagePlus image){CompositeImage ci=image instanceof CompositeImage?(CompositeImage)image:null;List<Object>luts=null;if(ci!=null){luts=new ArrayList<>();for(int i=0;i<image.getNChannels();i++){LUT lut=ci.getChannelLut(i+1);luts.add(o("channel",i,"min",number(lut.min),"max",number(lut.max),"rgb_interleaved_hex",table(lut)));}}return o("range_min",number(image.getDisplayRangeMin()),"range_max",number(image.getDisplayRangeMax()),"default16bit_range",ImagePlus.getDefault16bitRange(),"mode",ci==null?null:ci.getMode(),"projection",image.getProp("CompositeProjection"),"visibility",ci==null?null:ci.getActiveChannels().clone(),"channel_luts",luts);}
 static final class ActualContrastPlot extends ContrastPlot {int entered,returned;Object input;boolean log;@Override void setHistogram(ImageStatistics stats,boolean log){entered++;this.log=log;input=HistogramAutoReference.stats(stats);super.setHistogram(stats,log);returned++;}Object observation(){return o("identity",id(this),"class",getClass().getName(),"set_histogram_declaring_class","ij.plugin.frame.ContrastPlot","entered",entered,"super_returned",returned,"input_statistics",input,"log",log,"histogram_after",histogram==null?null:histogram.clone(),"hmax",hmax,"min",number(min),"max",number(max),"default_min",number(defaultMin),"default_max",number(defaultMax));}}
 static final class ObservedAdjuster extends ContrastAdjuster {volatile long assigned,completed;volatile Throwable workerError;int setups,setupImages,newImages,resets,autoCalls,setupReturns,setupImageReturns,newImageReturns,resetReturns,autoReturns,workerCalls,noPending;final List<Object>calls=new ArrayList<>();final List<Object>events=new ArrayList<>();ObservedAdjuster(){super();plot=new ActualContrastPlot();}@Override public void show(){}@Override public void setVisible(boolean v){super.setVisible(false);}@Override void setup(){setups++;String old=phase("setup");try{super.setup();setupReturns++;}finally{phase(old);}}@Override ImageProcessor setup(ImagePlus i){setupImages++;String old=phase("setup.ImagePlus");try{ImageProcessor actual=super.setup(i);setupImageReturns++;return actual;}finally{phase(old);}}@Override void setupNewImage(ImagePlus i,ImageProcessor p){newImages++;String old=phase("setupNewImage");try{super.setupNewImage(i,p);newImageReturns++;}finally{phase(old);}}@Override void reset(ImagePlus i,ImageProcessor p){resets++;String old=phase("reset");try{super.reset(i,p);resetReturns++;}finally{phase(old);}}@Override void autoAdjust(ImagePlus i,ImageProcessor p){autoCalls++;String old=phase("autoAdjust");try{super.autoAdjust(i,p);autoReturns++;}finally{phase(old);}}@Override public synchronized void actionPerformed(ActionEvent e){events.add(o("event_id",e.getID(),"event_identity",id(e),"source_identity",id(e.getSource()),"command",e.getActionCommand(),"sequence",assigned,"thread",thread(Thread.currentThread())));super.actionPerformed(e);}@Override void doUpdate(){long seq;boolean pending;Object before;synchronized(this){seq=assigned;pending=doReset||doAutoAdjust||doSet||doApplyLut||minSliderValue>=0||maxSliderValue>=0||brightnessValue>=0||contrastValue>=0;workerCalls++;if(!pending)noPending++;before=caFields(this);}Throwable error=null;try{super.doUpdate();}catch(RuntimeException|Error e){error=e;workerError=e;throw e;}finally{synchronized(this){calls.add(o("sequence",seq,"pending",pending,"before",before,"after",caFields(this),"thread",thread(Thread.currentThread()),"source_image_id",WindowManager.getCurrentImage()==null?null:WindowManager.getCurrentImage().getID(),"error",error==null?null:HistogramAutoReference.error(error),"super_returned",error==null));if(error==null&&seq>completed)completed=seq;notifyAll();}}}}
 static Object caFields(ObservedAdjuster c){return o("identity",id(c),"source_current_image_id",WindowManager.getCurrentImage()==null?null:WindowManager.getCurrentImage().getID(),"shift_key_down",IJ.shiftKeyDown(),"previous_image_id",c.previousImageID,"previous_type",c.previousType,"previous_slice",c.previousSlice,"auto_threshold",c.autoThreshold,"min",number(c.min),"max",number(c.max),"default_min",number(c.defaultMin),"default_max",number(c.defaultMax),"previous_min",number(c.previousMin),"previous_max",number(c.previousMax),"do_auto",c.doAutoAdjust,"do_reset",c.doReset,"do_set",c.doSet,"do_apply_lut",c.doApplyLut,"min_slider_value",c.minSliderValue,"max_slider_value",c.maxSliderValue,"brightness_value",c.brightnessValue,"contrast_value",c.contrastValue,"setups",c.setups,"setup_images",c.setupImages,"new_images",c.newImages,"resets",c.resets,"auto_calls",c.autoCalls,"setup_returns",c.setupReturns,"setup_image_returns",c.setupImageReturns,"new_image_returns",c.newImageReturns,"reset_returns",c.resetReturns,"auto_returns",c.autoReturns,"original_wrapper_declaring_class","ij.plugin.frame.ContrastAdjuster","worker_calls",c.workerCalls,"no_pending_calls",c.noPending,"assigned_sequence",c.assigned,"completed_sequence",c.completed,"done",c.done,"worker",thread(c.thread),"dialog",flags(c));}
 static Object button(Button b){List<Object> listeners=new ArrayList<>();boolean original=false;for(ActionListener x:b.getActionListeners()){listeners.add(x.getClass().getName());if(x==ca)original=true;}return o("identity",id(b),"label",b.getLabel(),"listeners",listeners,"original_listener_registered",original,"flags",flags(b),"action_handler_super_declaring_class","ij.plugin.frame.ContrastAdjuster");}
 static String text(){return recorder==null?"":recorder.getText();}
 static Object recorderState(){if(recorder==null)return null;return o("identity",id(recorder),"class",recorder.getClass().getName(),"flags",flags(recorder),"recording",Recorder.record,"script_mode",Recorder.scriptMode(),"choice_identity",id(recorderChoice),"mode",recorderChoice.getSelectedItem(),"mode_dispatch",recorderModeDispatch,"text",text());}
 static Object state(ImagePlus image){Object before=proc(image);Object stats=rawStats(image);return o("image",imageInfo(image),"roi",roi(image.getRoi()),"processor_before_raw_statistics",before,"raw_statistics",stats,"processor_after_raw_statistics",proc(image),"display",display(image),"contrast_adjuster",ca==null?null:caFields(ca),"plot",ca==null?null:((ActualContrastPlot)ca.plot).observation(),"host",host(image),"recorder",recorderState(),"raw_statistics_call_count",RAW_CALLS.size());}
 static void idle(ObservedAdjuster c)throws Exception{long end=localDeadline(500);while(System.nanoTime()<end){synchronized(c){if(c.workerError!=null)throw new IllegalStateException("Original worker failed",c.workerError);if(c.thread.getState()==Thread.State.WAITING)return;}Thread.sleep(1);}throw new IllegalStateException("Original ContrastAdjuster worker did not reach WAITING within500ms");}
 static Object dispatch(String op)throws Exception{ObservedAdjuster c=ca;Button b=op.equals("reset")?c.resetB:c.autoB;long seq=++nextSequence,end=localDeadline(500);Map<String,Object> ledger=null;while(ledger==null&&System.nanoTime()<end){ledger=edt(()->{synchronized(c){if(c.thread.getState()!=Thread.State.WAITING)return null;c.assigned=seq;Map<String,Object> item=o("sequence",seq,"operation",op,"monitor_held",Thread.holdsLock(c),"idle_worker",thread(c.thread),"button",button(b),"before",caFields(c),"handler_declaring_class","ij.plugin.frame.ContrastAdjuster","worker_run_declaring_class","ij.plugin.frame.ContrastAdjuster");ActionEvent event=new ActionEvent(b,ActionEvent.ACTION_PERFORMED,b.getActionCommand(),System.currentTimeMillis(),0);item.put("event_identity",id(event));item.put("event_id",event.getID());item.put("event_source_identity",id(event.getSource()));item.put("event_dispatch_thread",EventQueue.isDispatchThread());item.put("event_command",event.getActionCommand());b.dispatchEvent(event);item.put("after_listener",caFields(c));return item;}});if(ledger==null)Thread.sleep(1);}if(ledger==null)throw new IllegalStateException("No WAITING worker at real button dispatch within500ms");end=localDeadline(1000);synchronized(c){while(c.completed<seq&&c.workerError==null&&System.nanoTime()<end)c.wait(Math.max(1,Math.min(10,TimeUnit.NANOSECONDS.toMillis(end-System.nanoTime()))));if(c.workerError!=null)throw new IllegalStateException("Original doUpdate failed",c.workerError);if(c.completed!=seq)throw new IllegalStateException("Original doUpdate did not acknowledge sequence within1000ms");ledger.put("acknowledged_sequence",c.completed);ledger.put("worker_call",c.calls.get(c.calls.size()-1));}idle(c);return ledger;}
 static void findChoices(Container parent,List<Choice> out){for(Component c:parent.getComponents()){if(c instanceof Choice)out.add((Choice)c);if(c instanceof Container)findChoices((Container)c,out);}}
 static void openRecorder(String mode){recorder=new Recorder(false);List<Choice> choices=new ArrayList<>();findChoices(recorder,choices);if(choices.size()!=1)throw new IllegalStateException("Recorder mode Choice not unique");recorderChoice=choices.get(0);recorderChoice.select(mode);List<Object> listeners=new ArrayList<>();boolean actual=false;for(ItemListener listener:recorderChoice.getItemListeners()){listeners.add(listener.getClass().getName());if(listener==recorder)actual=true;}ItemEvent event=new ItemEvent(recorderChoice,ItemEvent.ITEM_STATE_CHANGED,mode,ItemEvent.SELECTED);recorderChoice.dispatchEvent(event);recorderModeDispatch=o("event_identity",id(event),"event_id",event.getID(),"state_change",event.getStateChange(),"source_identity",id(event.getSource()),"selected_item",mode,"listeners",listeners,"original_listener_registered",actual,"event_dispatch_thread",EventQueue.isDispatchThread());if(!text().isEmpty())throw new IllegalStateException("Recorder setup introduced recording text");hidden();}
 static void openCa(ImagePlus image)throws Exception{edt(()->{bind(image);ca=new ObservedAdjuster();ca.run("");return null;});idle(ca);}
 static Object closeCa()throws Exception{if(ca==null)return null;ObservedAdjuster c=ca;long until=localDeadline(500);Object before=null;while(before==null&&System.nanoTime()<until){before=edt(()->{synchronized(c){if(c.thread.getState()!=Thread.State.WAITING)return null;Object value=caFields(c);c.close();return value;}});if(before==null)Thread.sleep(1);}if(before==null)throw new IllegalStateException("Original CA worker not WAITING before close");c.thread.join(Math.max(1,TimeUnit.NANOSECONDS.toMillis(localDeadline(500)-System.nanoTime())));Object after=edt(()->caFields(c));if(c.thread.isAlive())throw new IllegalStateException("Original ContrastAdjuster thread did not join within500ms");ca=null;return o("before",before,"after",after,"calls",new ArrayList<>(c.calls),"events",new ArrayList<>(c.events),"joined",!c.thread.isAlive());}
 static Object closeHost(ImagePlus image)throws Exception{Host h=HOSTS.get(image);Thread t=h.window instanceof StackWindow?(Thread)field(StackWindow.class,h.window,"thread"):null;long until=localDeadline(500);Object before=null;while(before==null&&System.nanoTime()<until){before=edt(()->{synchronized(h.window){if(t!=null&&t.getState()!=Thread.State.WAITING)return null;Object observed=host(image);image.changes=false;if(!h.window.close())throw new IllegalStateException("Original image window refused close");return observed;}});if(before==null)Thread.sleep(1);}if(before==null)throw new IllegalStateException("Original StackWindow worker did not become WAITING before close");if(t!=null)t.join(Math.max(1,TimeUnit.NANOSECONDS.toMillis(localDeadline(500)-System.nanoTime())));if(t!=null&&t.isAlive())throw new IllegalStateException("Original StackWindow worker did not join within500ms");Object original=before;return edt(()->{HOSTS.remove(image);return o("before",original,"window_after",flags(h.window),"stack_worker",thread(t),"stack_worker_joined",t==null||!t.isAlive());});}

 static final class Clone {final ImageStack stack;final Calibration calibration;final Roi roi;final int c,z,t,mode,channels,slices,frames;final LUT[] tables;final boolean[] visibility;final String projection;final double low,high;final boolean composite;final java.awt.image.ColorModel processorLut;Clone(ImagePlus image){processorLut=image.getProcessor().getColorModel();stack=image.getStack().duplicate();calibration=image.getCalibration().copy();roi=image.getRoi()==null?null:(Roi)image.getRoi().clone();channels=image.getNChannels();slices=image.getNSlices();frames=image.getNFrames();c=image.getChannel();z=image.getSlice();t=image.getFrame();low=image.getDisplayRangeMin();high=image.getDisplayRangeMax();composite=image instanceof CompositeImage;if(composite){CompositeImage ci=(CompositeImage)image;tables=new LUT[ci.getNChannels()];for(int i=0;i<tables.length;i++)tables[i]=(LUT)ci.getChannelLut(i+1).clone();mode=ci.getMode();visibility=ci.getActiveChannels().clone();}else{tables=null;mode=0;visibility=null;}projection=image.getProp("CompositeProjection");}}

 static final class Replay {final String context,label,delta;final Clone source;final Object originalBefore,originalAfter;Replay(String context,String label,String delta,Clone source,Object before,Object after){this.context=context;this.label=label;this.delta=delta;this.source=source;originalBefore=before;originalAfter=after;}}
 static ImagePlus replayImage(Clone captured,String name){ImagePlus base=new ObservedImagePlus(name,captured.stack.duplicate());base.setDimensions(captured.channels,captured.slices,captured.frames);ImagePlus image=captured.composite?new ObservedCompositeImage(base,CompositeImage.COMPOSITE):base;image.setOpenAsHyperStack(image.getStackSize()>1);Host h=new Host(image);HOSTS.put(image,h);bind(image);image.setCalibration(captured.calibration.copy());if(captured.composite){CompositeImage ci=(CompositeImage)image;for(int i=0;i<captured.tables.length;i++)ci.setChannelLut((LUT)captured.tables[i].clone(),i+1);if(captured.projection!=null)ci.setProp("CompositeProjection",captured.projection);ci.setMode(captured.mode);boolean[] flags=ci.getActiveChannels();System.arraycopy(captured.visibility,0,flags,0,Math.min(flags.length,captured.visibility.length));}image.setPosition(captured.c,captured.z,captured.t);image.setDisplayRange(captured.low,captured.high);image.getProcessor().setColorModel(captured.processorLut);image.setRoi(captured.roi==null?null:(Roi)captured.roi.clone(),false);h.canvas.setSourceRect(new Rectangle(0,0,image.getWidth(),image.getHeight()));h.canvas.setDrawingSize(image.getWidth()*2,image.getHeight()*2);h.canvas.setMagnification(2);return image;}
 static Object perform(Map<String,Object> input,ImagePlus image,ImagePlus other,Map<String,Object> fixture)throws Exception{String op=(String)input.get("operation");List<Object> subevents=new ArrayList<>();if(op.equals("auto")||op.equals("reset")||op.equals("reset_then_12bit")){boolean shift=Boolean.TRUE.equals(input.get("shift"));edt(()->{if(shift)IJ.setKeyDown(KeyEvent.VK_SHIFT);else IJ.setKeyUp(KeyEvent.VK_SHIFT);return null;});Object event=dispatch(op.equals("auto")?"auto":"reset");if(op.equals("reset_then_12bit"))edt(()->{subevents.add(o("api","ImagePlus.setDefault16bitRange","before",ImagePlus.getDefault16bitRange(),"state_before",state(image),"requested",12));ImagePlus.setDefault16bitRange(12);subevents.add(o("api","ImagePlus.setDefault16bitRange.return","actual",ImagePlus.getDefault16bitRange(),"state_after",state(image)));return null;});edt(()->{IJ.setKeyUp(KeyEvent.VK_SHIFT);return null;});return o("button_dispatch",event,"subevents",subevents);}return edt(()->{if(op.equals("set_roi"))subevents.add(assignRoi(image,input.get("roi")));else if(op.equals("numeric_set")){List<Object>range=l(input.get("range"));image.setDisplayRange(d(range.get(0)),d(range.get(1)));subevents.add(o("api","ImagePlus.setDisplayRange","range",range,"after",state(image)));}else if(op.equals("navigate")){List<Object>p=l(input.get("position_tcz"));image.setPosition(n(p.get(1))+1,n(p.get(2))+1,n(p.get(0))+1);subevents.add(o("api","ImagePlus.setPosition","position_tcz",p,"after",state(image)));}else if(op.equals("public_update")){ContrastAdjuster.update();subevents.add(o("api","ContrastAdjuster.update"));}else if(op.equals("restore_fixture")){configure(image,fixture);subevents.add(assignRoi(image,fixture.get("roi")));subevents.add(o("api","restore_requested_fixture_display_no_cycle_write"));}else if(op.equals("owner_away_back")){WindowManager.setCurrentWindow(HOSTS.get(other).window);WindowManager.setTempCurrentImage(other);WindowEvent away=new WindowEvent(ca,WindowEvent.WINDOW_ACTIVATED,HOSTS.get(image).window);ca.dispatchEvent(away);subevents.add(o("api","WindowManager.setCurrentWindow","source_image_id",other.getID(),"event_id",away.getID(),"event_identity",id(away),"event_source_identity",id(away.getSource()),"event_dispatch_thread",EventQueue.isDispatchThread(),"opposite_window_identity",id(away.getOppositeWindow()),"ca_after",caFields(ca)));WindowManager.setCurrentWindow(HOSTS.get(image).window);WindowManager.setTempCurrentImage(image);WindowEvent back=new WindowEvent(ca,WindowEvent.WINDOW_ACTIVATED,HOSTS.get(other).window);ca.dispatchEvent(back);subevents.add(o("api","WindowManager.setCurrentWindow","source_image_id",image.getID(),"event_id",back.getID(),"event_identity",id(back),"event_source_identity",id(back.getSource()),"event_dispatch_thread",EventQueue.isDispatchThread(),"opposite_window_identity",id(back.getOppositeWindow()),"ca_after",caFields(ca)));}else if(!op.equals("redraw")&&!op.equals("statistics"))throw new IllegalArgumentException("Undeclared operation "+op);if(Boolean.TRUE.equals(input.get("redraw"))||op.equals("redraw")){image.updateAndDraw();subevents.add(o("api","ImagePlus.updateAndDraw","explicit",true,"after",state(image)));}return o("button_dispatch",null,"subevents",subevents);});}
 static Object environment(){Map<String,Object> loaded=new LinkedHashMap<>();for(Class<?> c:new Class<?>[]{ImageJ.class,ImagePlus.class,CompositeImage.class,ImageCanvas.class,ImageWindow.class,StackWindow.class,ContrastAdjuster.class,ContrastPlot.class,Recorder.class,HistogramAutoReference.class,ObservedImagePlus.class,ObservedCompositeImage.class,ObservedAdjuster.class,ActualContrastPlot.class,ImageStatistics.class,ByteStatistics.class,ShortStatistics.class,FloatStatistics.class,Calibration.class,ShapeRoi.class,StackStatistics.class,WindowManager.class})loaded.put(c.getName(),o("resource",c.getResource("/"+c.getName().replace('.','/')+".class").toString(),"code_source",c.getProtectionDomain().getCodeSource().getLocation().toString()));return o("imagej_version",IJ.getVersion(),"java_version",System.getProperty("java.version"),"java_vendor",System.getProperty("java.vendor"),"os_name",System.getProperty("os.name"),"awt_headless",GraphicsEnvironment.isHeadless(),"event_dispatch_thread",EventQueue.isDispatchThread(),"gui_scale",number(Prefs.getGuiScale()),"imagej",flags(app),"loaded_classes",loaded,"live_reference_hosts",HOSTS.size(),"scope","hidden original ImageCanvas/ImageWindow or StackWindow; inherited ContrastAdjuster buttons and worker; offscreen paint; Recorder(false); no OS input or product acceptance");}
 static Object capture(Map<String,Object> spec)throws Exception{long started=System.nanoTime();deadline=started+TimeUnit.SECONDS.toNanos(5);RAW_CALLS.clear();rawSequence=0;Map<String,Object> beforeSources=new LinkedHashMap<>(),afterSources=new LinkedHashMap<>(),setups=new LinkedHashMap<>(),fixtures=new LinkedHashMap<>();List<Object> stages=new ArrayList<>(),comparisons=new ArrayList<>(),comparisonActions=new ArrayList<>(),cleanups=new ArrayList<>();List<Replay> replays=new ArrayList<>();for(Object value:l(spec.get("groups"))){Map<String,Object> group=m(value),fixture=m(group.get("fixture"));String context=(String)group.get("name"),name=(String)fixture.get("name");ImagePlus image=edt(()->{ImagePlus.setDefault16bitRange(0);ImagePlus built=build(fixture,setups);fixtures.put(name,fixture);return built;});ImagePlus other=group.get("other")==null?null:edt(()->{Map<String,Object>otherSpec=m(group.get("other"));ImagePlus built=build(otherSpec,setups);fixtures.put((String)otherSpec.get("name"),otherSpec);return built;});edt(()->{bind(image);beforeSources.put(name,source(image));if(other!=null)beforeSources.put((String)m(group.get("other")).get("name"),source(other));ImagePlus.setDefault16bitRange(0);return null;});if(!Boolean.TRUE.equals(group.get("statistics_only"))){openCa(image);edt(()->{openRecorder((String)group.get("recorder_mode"));return null;});}for(Object action:l(group.get("actions"))){Map<String,Object> input=m(action);String label=(String)input.get("label");Object before=edt(()->state(image));String textBefore=edt(()->text());Clone clone=Boolean.TRUE.equals(group.get("compare_recorded_autos"))&&input.get("operation").equals("auto")?edt(()->new Clone(image)):null;Object delivered=perform(input,image,other,fixture);Object handler=edt(()->state(image));String textAfter=edt(()->text());if(!textAfter.startsWith(textBefore))throw new IllegalStateException("Recorder text ceased to be append-only");String delta=textAfter.substring(textBefore.length());Object rgb=input.get("operation").equals("statistics")?null:edt(()->paint(image,context+"."+label));Object after=edt(()->state(image));stages.add(o("ordinal",stages.size(),"label",label,"context",context,"fixture",name,"operation",input.get("operation"),"input",input,"before",before,"dispatch",delivered,"after_handler",handler,"rgb",rgb,"after",after,"recorder_before",textBefore,"recorder_after",textAfter,"recorder_delta",delta));if(clone!=null)replays.add(new Replay(context,label,delta,clone,before,handler));remaining();}Object recorderFinal=edt(()->{Object finalState=recorderState();if(recorder!=null)recorder.close();recorder=null;recorderChoice=null;return finalState;});Object closed=closeCa();edt(()->{afterSources.put(name,source(image));if(other!=null)afterSources.put((String)m(group.get("other")).get("name"),source(other));return null;});List<Object> hostClose=new ArrayList<>();hostClose.add(closeHost(image));if(other!=null)hostClose.add(closeHost(other));cleanups.add(o("context",context,"recorder_final",recorderFinal,"contrast_adjuster",closed,"hosts",hostClose));}
 for(Replay replay:replays){if(!replay.delta.contains("Enhance Contrast")){comparisons.add(o("context",replay.context,"original_label",replay.label,"recorded_delta",replay.delta,"executed",false,"reason","actual_Auto_delta_has_no_Enhance_Contrast_line"));continue;}ImagePlus image=edt(()->replayImage(replay.source,"comparison."+replay.context+"."+replay.label));Object sourceBefore=edt(()->source(image)),before=edt(()->state(image));Object call=edt(()->{ImagePlus current=WindowManager.getCurrentImage();long time=System.nanoTime();IJ.run(image,"Enhance Contrast","saturated=0.35");return o("api","IJ.run(ImagePlus,String,String)","command","Enhance Contrast","options","saturated=0.35","image_id",image.getID(),"bound_before_id",current==null?null:current.getID(),"thread",thread(Thread.currentThread()),"elapsed_ms",(System.nanoTime()-time)/1e6);});Object afterHandler=edt(()->state(image)),rgb=edt(()->paint(image,"comparison."+replay.label)),after=edt(()->state(image)),sourceAfter=edt(()->source(image));comparisons.add(o("context",replay.context,"original_label",replay.label,"recorded_delta",replay.delta,"executed",true,"source_before",sourceBefore,"before",before,"dispatch",call,"after_handler",afterHandler,"rgb",rgb,"after",after,"source_after",sourceAfter,"cleanup",closeHost(image)));}
 for(Object value:l(spec.get("groups"))){Map<String,Object>group=m(value);if(group.get("comparison_action")==null)continue;List<Object>indices=new ArrayList<>();for(int i=0;i<comparisons.size();i++)if(m(comparisons.get(i)).get("context").equals(group.get("name")))indices.add(i);int cleanupIndex=-1;for(int i=0;i<cleanups.size();i++)if(m(cleanups.get(i)).get("context").equals(group.get("name")))cleanupIndex=i;comparisonActions.add(o("context",group.get("name"),"input",group.get("comparison_action"),"comparison_indices",indices,"original_cleanup_index",cleanupIndex,"after_all_original_contexts_closed",true));}Object env=edt(()->{ImagePlus.setDefault16bitRange(0);hidden();return environment();});return o("contract",CONTRACT,"id",spec.get("id"),"input_contract",spec,"fixtures",fixtures,"source_before",beforeSources,"host_setup",setups,"stages",stages,"source_after",afterSources,"comparisons",comparisons,"comparison_actions",comparisonActions,"environment",env,"cleanup",cleanups,"raw_statistics_calls",new ArrayList<>(RAW_CALLS),"elapsed_ms",(System.nanoTime()-started)/1e6);}
 static Object probe()throws Exception{long started=System.nanoTime();deadline=started+TimeUnit.SECONDS.toNanos(5);Map<String,Object>setup=new LinkedHashMap<>();Map<String,Object> fixture=__PROBE__;ImagePlus image=edt(()->build(fixture,setup));openCa(image);edt(()->{openRecorder("Macro");return null;});Object before=edt(()->state(image)),auto=dispatch("auto"),autoAfter=edt(()->state(image)),reset=dispatch("reset"),resetAfter=edt(()->state(image));Object paint=edt(()->paint(image,"environment_probe"));Object modes=edt(()->{Object macro=recorderState();recorder.close();recorder=null;openRecorder("Java");Object java=recorderState();recorder.close();recorder=null;recorderChoice=null;return o("Macro",macro,"Java",java);});Object close=closeCa();Object hostClose=closeHost(image);return o("contract",CONTRACT,"kind","environment_probe_no_H_cases","host_setup",setup,"before",before,"auto_dispatch",auto,"auto_after",autoAfter,"reset_dispatch",reset,"reset_after",resetAfter,"paint",paint,"recorder_modes",modes,"cleanup",o("contrast_adjuster",close,"host",hostClose),"environment",edt(()->environment()),"raw_statistics_calls",new ArrayList<>(RAW_CALLS),"elapsed_ms",(System.nanoTime()-started)/1e6);}
 public static void main(String[] args){try{deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(10);edt(()->{app=new ImageJ(ImageJ.NO_SHOW);Prefs.setGuiScale(1);Prefs.autoContrast=false;Prefs.useInvertingLut=false;ImagePlus.setDefault16bitRange(0);hidden();return null;});if(args.length!=1)throw new IllegalArgumentException("One explicit mode required");if(args[0].equals("--probe"))System.out.println(json(encodeTables(probe())));else if(args[0].equals("--capture")){for(Object spec:CASES){String raw=json(encodeObservations(encodeTables(capture(m(spec)))));remaining();System.out.println(raw);System.out.flush();}}else throw new IllegalArgumentException("Unknown mode");deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(2);edt(()->{hidden();app.dispose();return null;});System.exit(0);}catch(Throwable error){error.printStackTrace(System.err);System.exit(2);}}
}
'''


def probe_fixture():
    return fixture("environment_probe", shape=(2, 2, 2, 3, 5), position=(1, 1, 1),
                   ranges=[[0, 31], [2, 47]], flags=[True, False])


def input_plan(verified):
    rows = cases()
    return {"contract": CONTRACT, "ordered_case_ids": list(CASE_IDS), "cases": rows,
            "cases_canonical_sha256": lineage.canonical_sha(rows),
            "ordered_stage_labels": {row["id"]: [g["name"]+"."+a["label"] for g in row["groups"] for a in g["actions"]] for row in rows},
            "ordered_comparison_action_labels": {row["id"]: [g["name"]+"."+g["comparison_action"]["label"] for g in row["groups"] if g["comparison_action"] is not None] for row in rows},
            "output_membership": {"raw_jsonl_rows": 16, "original_stages": 80,
                "source_before_after_pairs": 28, "real_button_dispatches": 58,
                "Auto_button_dispatches": 51, "Reset_button_dispatches": 7,
                "per_case_stages": [5, 1, 2, 1, 2, 2, 3, 1, 4, 1, 2, 3, 9, 3, 15, 26],
                "comparison_actions": 3, "total_declared_actions_including_comparisons": 83, "pre_Auto_comparison_candidates": 15,
                "executed_comparisons": "Only actual Auto Recorder deltas containing an Enhance Contrast command; fallback/no-command candidates retain unexecuted observations",
                "RGB": "one per non-statistics original stage and actually executed comparison; statistics-only stages have null RGB"},
            "bounds": BOUNDS, "deadlines_ms": {"idle": 500, "event_completion": 1000, "worker_join": 500},
            "probe_fixture": probe_fixture(), "entry_contracts": verified["histogram_auto_entries"],
            "environment_inputs": {"ImageJ_constructor": "ImageJ.NO_SHOW", "gui_scale": 1,
                "auto_contrast": False, "use_inverting_lut": False,
                "image_observers": {"ordinary": CLASS_NAME+"$ObservedImagePlus", "composite": CLASS_NAME+"$ObservedCompositeImage",
                    "method": "getRawStatistics; super once; return identical object; clone actual fields/counts with phase/caller/thread/sequence"},
                "host_setup_order": HOST_ORDER, "magnification": 2,
                "paint_method": PAINT_METHOD,
                "event_order": ["snapshot before raw-statistics getter (processor ROI/mask)", "actual getter observations",
                    "worker WAITING observed with ContrastAdjuster monitor held", "EDT actual Button.dispatchEvent",
                    "original listener", "original worker super.doUpdate return acknowledgement", "handler state before paint",
                    "original ImageCanvas offscreen paint", "same image RGB readback", "after state"],
                "observer_wrappers": "setup/setup(ImagePlus)/setupNewImage/reset/autoAdjust/doUpdate and ContrastPlot.setHistogram; super exactly once; no decision logic copied",
                "recorder": "Recorder(false); real unique Choice select and SELECTED ItemEvent to original registered listener; Macro then Java for H16",
                "source_binding": "actual current ImageWindow and EDT temporary current image; worker global source identity observed",
                "H16_comparisons": "all13 original actions and complete Recorder text first; close all original objects; fresh pre-Auto-clone hosts one at a time"},
            "table_encoding": "observed_rgb_hex_intern_v1",
            "observation_encoding": "observed_json_subtree_intern_v1",
            "observation_pool": {
                "scope": "H01-H16 case rows only; environment probe retains original LUT-only transport",
                "pool": "observed_json_values; strings/maps/lists only; unique canonical encoded values; every entry transitively referenced",
                "reference": {"observed_json_value": "exact one-field object; nonbool integer index"},
                "topology": "postorder first-use indices; references inside pool entry i strictly less than i",
                "canonical_identity": "JSON of encoded children with recursively sorted map keys; original string/numeric values preserved",
                "eligibility": {"string_min_utf16_units": 24, "container_min_ascii_json_bytes": 96},
                "direct_roots": ["contract", "id", "input_contract", "fixtures", "environment", "table_encoding", "observed_lut_tables", "comparison_actions"],
                "direct_stage_fields": ["ordinal", "label", "context", "fixture", "operation", "input"],
                "direct_comparison_fields": ["context", "original_label", "executed", "reason"],
                "envelopes": "stages/comparisons arrays and their item maps remain direct; only nonprotected values are pooled",
                "reserved_keys": ["observed_json_value", "observation_encoding", "observed_json_values"],
                "guards": {"max_depth": 128, "max_pool_entries_per_row": 65536,
                           "max_fully_expanded_json_bytes_combined": 33554432},
                "decoding": "validate encoded provenance first; exact grammar/index/topology/uniqueness/full transitive use; expand observations then LUTs before behavioral assertions; logical guard counts fully expanded actual JSON after BOTH layers across all16 rows; original4MiB physical payload cap unchanged"},
            "transport": {"observed_lut_tables": {"pool": "unique actual1536-character lowercase RGB hex strings, every entry referenced", "keys": ["rgb_interleaved_hex", "lut_rgb_interleaved_hex"], "reference": {"observed_lut_table": "nonbool integer pool index"}, "nulls": "preserved", "decoding": "validate exact one-field refs, index range, pool uniqueness/hex/full usage then expand before assertions"}, "float": {"value": "finite JSON number or null", "classification": ["finite", "nan", "positive_infinity", "negative_infinity"], "ieee754_f64_le_hex": "exact little-endian raw double bits"},
                "histogram16": "null or {encoding:int32_nonzero_pairs,length:actual_length,nonzero:[[index,actual_nonzero_count],...]}; omitted cells zero",
                "source_bits": "little-endian declared storage plus actual Java widened binary64 bits",
                "processor_calibration_table": "length and raw binary32 bits SHA256; density function/coefficients retained separately"},
            "scope": "original hidden ImagePlus/CompositeImage statistics and ContrastAdjuster/Recorder event-worker observations only; no OS input, product acceptance or eligibility"}


def stage_path(path):
    path = reference._reference_path(path)
    if not path.is_relative_to(STAGE.resolve()) or path == STAGE.resolve():
        raise ValueError("Use a fresh child of the owned Histogram/Auto reference stage")
    return path


def write_json(path, value):
    raw = (json.dumps(value, indent=2, ensure_ascii=True, allow_nan=False)+"\n").encode("utf-8")
    if len(raw) > BOUNDS["combined_payload_bytes"]:
        raise ValueError("Reference JSON exceeds payload bound")
    with path.open("xb") as stream:
        stream.write(raw)


def payload_size(stage):
    size = sum(p.stat().st_size for p in stage.rglob("*") if p.is_file() and p.suffix in (".json", ".stdout", ".stderr"))
    if size > BOUNDS["combined_payload_bytes"]:
        raise ValueError("Combined input/raw/report payload exceeds4MiB")
    reference._storage(stage)
    return size


def inputs(deadline):
    if reference.digest(Path(lineage.__file__).resolve()) != LINEAGE_SHA:
        raise ValueError("Qualified generic reference supervisor changed")
    for name, sha in ENTRIES.items():
        if reference.digest(ROOT/name) != sha:
            raise ValueError("Frozen Histogram/Auto entry changed: "+name)
    verified = lineage.inputs(deadline)
    base = json.loads((ROOT/next(iter(ENTRIES))).read_text(encoding="utf-8"))
    sources = {row["relative_path"]: {"bytes": row["bytes"], "sha256": row["sha256"]} for row in base["reference"]["source_members"]}
    for name, sha in EXTRA_SOURCE_PINS.items():
        path = reference.REUSE/"sources"/name
        sources[name] = {"bytes": path.stat().st_size, "sha256": sha}
    for name, expected in sources.items():
        path = reference.REUSE/"sources"/name
        if path.stat().st_size != expected["bytes"] or reference.digest(path) != expected["sha256"]:
            raise ValueError("Histogram behavioral source pin differs: "+name)
    for name in ("ContrastAdjuster", "ContrastPlot", "Recorder"):
        if not (lineage.REUSED_CLASSES/"ij/plugin/frame"/(name+".class")).is_file():
            raise ValueError("Qualified class tree lacks required original consumer: "+name)
    verified.update({"histogram_auto_entries": [lineage.pin(ROOT/name) for name in ENTRIES],
                     "histogram_auto_source_members": sources,
                     "qualified_supervisor_source": lineage.pin(Path(lineage.__file__).resolve())})
    return verified


def begin(stage, action_name):
    if stage.exists():
        raise ValueError("Output stage must be fresh; retain all prior attempts")
    stage.mkdir(parents=True)
    for name in ("home", "tmp", "tool-sources", "empty-sourcepath"):
        (stage/name).mkdir()
    for tool in (Path(__file__).resolve(), Path(lineage.__file__).resolve(), Path(reference.__file__).resolve(), ROOT/"scripts/capture_binary_reference.py"):
        shutil.copyfile(tool, stage/"tool-sources"/tool.name)
    write_json(stage/"attempt.json", {"contract": CONTRACT, "action": action_name, "harness": lineage.pin(Path(__file__).resolve()),
               "state": "attempt_started_no_acceptance", "ordered_case_ids": list(CASE_IDS), "bounds": BOUNDS})


def run(argv, stage, label, seconds, deadline):
    seconds = min(seconds, int(deadline-time.monotonic()-2))
    if seconds < 1:
        raise TimeoutError("No remaining parent process budget")
    write_json(stage/(label+".command.json"), {"argv": argv, "timeout_seconds": seconds,
               "parent_remaining_seconds": deadline-time.monotonic(), "scope": "reference observer only; no OS input or product acceptance"})
    reference.MAX_OUTPUT = BOUNDS["combined_payload_bytes"]-sum(p.stat().st_size for p in stage.rglob("*.json"))-65536
    if reference.MAX_OUTPUT < 1:
        raise ValueError("No bounded raw-output space remains")
    return reference._run(argv, stage, label, seconds)


def java_command(stage, prepared, mode):
    return [str(reference.REUSE/"jdk/bin/java.exe"), "-Xmx256m", "-Djava.awt.headless=false",
            "-Duser.home="+str(stage/"home"), "-Djava.io.tmpdir="+str(stage/"tmp"), "-Dij.dir="+str(stage),
            "-cp", os.pathsep.join((str(prepared/"classes"), str(lineage.REUSED_CLASSES))), CLASS_NAME, mode]


def prepare(stage):
    stage = stage_path(stage)
    deadline = time.monotonic()+BOUNDS["prepare_parent_seconds"]
    begin(stage, "prepare")
    processes = {}
    try:
        verified = inputs(deadline)
        plan = input_plan(verified)
        write_json(stage/"verified-inputs.json", verified)
        write_json(stage/"input-plan.json", plan)
        driver = stage/"HistogramAutoReference.java"
        driver.write_bytes(DRIVER.replace("__CASES__", lineage.java_value(cases())).replace("__HOST_ORDER__", lineage.java_value(HOST_ORDER))
                           .replace("__PROBE__", lineage.java_value(probe_fixture())).encode("utf-8"))
        (stage/"classes").mkdir()
        write_json(stage/"execution-inputs.json", {"driver": lineage.pin(driver), "harness": lineage.pin(Path(__file__).resolve()),
            "input_plan": lineage.pin(stage/"input-plan.json"), "verified_inputs": lineage.pin(stage/"verified-inputs.json"),
            "tools": [lineage.pin(p) for p in sorted((stage/"tool-sources").iterdir())]})
        processes["compile"] = run([str(reference.REUSE/"jdk/bin/javac.exe"), "-J-Xmx256m", "-encoding", "UTF-8", "-proc:none", "-implicit:none",
            "-sourcepath", str(stage/"empty-sourcepath"), "-classpath", str(lineage.REUSED_CLASSES), "-d", str(stage/"classes"), str(driver)], stage, "javac", 60, deadline)
        processes["version"] = run([str(reference.REUSE/"jdk/bin/java.exe"), "-Xmx128m", "-version"], stage, "version", 10, deadline)
        processes["probe"] = run(java_command(stage, stage, "--probe"), stage, "probe", 15, deadline)
        probe = json.loads((stage/"probe.stdout").read_text(encoding="utf-8"))
        if probe["contract"] != CONTRACT or probe["kind"] != "environment_probe_no_H_cases" or probe["elapsed_ms"] > 5000:
            raise ValueError("Wrong environment-only probe or case deadline")
        for key in ("auto_dispatch", "reset_dispatch"):
            event = probe[key]
            if (not event["monitor_held"] or event["idle_worker"]["state"] != "WAITING" or not event["event_dispatch_thread"]
                    or not event["button"]["original_listener_registered"] or event["acknowledged_sequence"] != event["sequence"]
                    or not event["worker_call"]["super_returned"] or event["worker_call"]["thread"]["event_dispatch_thread"]
                    or event["worker_call"]["source_image_id"] != probe["before"]["image"]["id"]):
                raise ValueError("Actual original button/worker acknowledgement feasibility failed")
        host = probe["before"]["host"]
        selected = probe_fixture()["planes"][-1]["bits_hex"]
        painted = probe["cleanup"]["host"]["before"]
        if (host["position_tcz"] != [1, 1, 1] or host["current_slice_index"] != 7
                or host["selector_position_czt"] != [1, 1, 1]
                or not host["current_window_is_host"] or not host["current_image_is_source"]
                or not host["displayed_hyperstack"] or not host["window_hyperstack"]
                or host["window"]["visible"] or host["window"]["showing"]
                or host["canvas"]["magnification"]["value"] != 2
                or host["canvas"]["source_rect_xywh"] != [0, 0, 5, 3]
                or host["canvas"]["drawing_wh"] != [10, 6]
                or probe["before"]["processor_before_raw_statistics"]["bits_hex"] != selected
                or not probe["before"]["processor_before_raw_statistics"]["pixels_are_source"]
                or painted["offscreen_paint"]["count"] != 1
                or painted["offscreen_paint"]["method"] != PAINT_METHOD
                or painted["offscreen_paint"]["last_label"] != "environment_probe"):
            raise ValueError("Original hidden scientific host/source/paint feasibility failed")
        if not probe["cleanup"]["contrast_adjuster"]["joined"] or probe["environment"]["live_reference_hosts"] != 0:
            raise ValueError("Probe did not release original worker/hosts")
        if (probe["environment"]["awt_headless"] or probe["environment"]["imagej_version"] != "1.54r"
                or probe["environment"]["imagej"]["visible"] or probe["environment"]["imagej"]["showing"]):
            raise ValueError("Wrong original AWT/ImageJ environment")
        for name, mode in probe["recorder_modes"].items():
            if (mode["mode"] != name or not mode["mode_dispatch"]["original_listener_registered"]
                    or not mode["mode_dispatch"]["event_dispatch_thread"] or mode["script_mode"] != (name == "Java")
                    or mode["flags"]["visible"] or mode["flags"]["showing"]):
                raise ValueError("Actual Recorder Choice mode dispatch failed")
        classes = reference._files(stage/"classes", BOUNDS["fresh_stage_bytes"], deadline)
        if any(not name.startswith("ij/plugin/frame/HistogramAutoReference") or not name.endswith(".class") for name in classes):
            raise ValueError("Prepare compiled something other than new observer classes")
        if inputs(deadline) != verified:
            raise ValueError("Qualified inputs changed during prepare")
        report = {"contract": CONTRACT, "state": "prepared_environment_only_H01_H16_not_executed", "harness": lineage.pin(Path(__file__).resolve()),
                  "input_plan": lineage.pin(stage/"input-plan.json"), "verified_inputs": lineage.pin(stage/"verified-inputs.json"),
                  "execution_inputs": lineage.pin(stage/"execution-inputs.json"), "driver": lineage.pin(driver),
                  "prepared_classes": classes, "processes": processes, "probe": probe}
        write_json(stage/"prepared.json", report)
        payload_size(stage)
        return report
    except BaseException as error:
        write_json(stage/"failure.json", {"contract": CONTRACT, "state": "prepare_failed_no_H_credit", "error": repr(error),
                   "traceback": traceback.format_exc(), "completed_processes": processes})
        raise


def capture(stage, prepared):
    stage, prepared = stage_path(stage), stage_path(prepared)
    started = time.monotonic()
    deadline = started+BOUNDS["capture_parent_seconds"]
    begin(stage, "capture")
    process = None
    try:
        held = json.loads((prepared/"prepared.json").read_text(encoding="utf-8"))
        if held["harness"] != lineage.pin(Path(__file__).resolve()):
            raise ValueError("Held producer differs; prepare a fresh attempt")
        for key, filename in (("input_plan", "input-plan.json"), ("verified_inputs", "verified-inputs.json"),
                              ("execution_inputs", "execution-inputs.json"), ("driver", "HistogramAutoReference.java")):
            if held[key] != lineage.pin(prepared/filename):
                raise ValueError("Prepared input differs: "+filename)
        verified = json.loads((prepared/"verified-inputs.json").read_text(encoding="utf-8"))
        if verified != inputs(deadline) or held["prepared_classes"] != reference._files(prepared/"classes", BOUNDS["fresh_stage_bytes"], deadline):
            raise ValueError("Qualified inputs or prepared observer classes differ")
        plan = json.loads((prepared/"input-plan.json").read_text(encoding="utf-8"))
        if plan != input_plan(verified):
            raise ValueError("Fixed H01--H16 input plan differs")
        shutil.copyfile(prepared/"input-plan.json", stage/"input-plan.json")
        write_json(stage/"execution-inputs.json", {"prepared": lineage.pin(prepared/"prepared.json"), "harness": held["harness"],
            "input_plan": held["input_plan"], "verified_inputs": held["verified_inputs"], "driver": held["driver"],
            "ordered_case_ids": list(CASE_IDS), "bounds": BOUNDS})
        command = java_command(stage, prepared, "--capture")
        write_json(stage/"command.json", {"argv": command, "parent_seconds": 120, "maximum_process_seconds": 90,
            "prepared": lineage.pin(prepared/"prepared.json"), "input_plan": lineage.pin(stage/"input-plan.json"), "ordered_case_ids": list(CASE_IDS)})
        process = run(command, stage, "reference", 90, deadline)
        rows = [json.loads(line) for line in (stage/"reference.stdout").read_text(encoding="utf-8").splitlines() if line.strip()]
        if tuple(row.get("id") for row in rows) != CASE_IDS:
            raise ValueError("Exact ordered H01--H16 raw membership missing")
        for row, spec in zip(rows, cases()):
            expected = [(g["name"], a) for g in spec["groups"] for a in g["actions"]]
            actual = [(s["context"], s["input"]) for s in row["stages"]]
            if row["contract"] != CONTRACT or row["input_contract"] != spec or actual != expected or row["elapsed_ms"] > 5000:
                raise ValueError("Raw case does not retain declared inputs/actions/deadline")
        if inputs(deadline) != verified or reference._files(prepared/"classes", BOUNDS["fresh_stage_bytes"], deadline) != held["prepared_classes"]:
            raise ValueError("Consumed reference inputs changed during capture")
        report = {"contract": CONTRACT, "state": "raw_observations_only_no_numerical_or_product_acceptance",
            "prepared": lineage.pin(prepared/"prepared.json"), "harness": held["harness"], "process": process,
            "raw": lineage.pin(stage/"reference.stdout"), "input_plan": lineage.pin(stage/"input-plan.json"),
            "execution_inputs": lineage.pin(stage/"execution-inputs.json"), "ordered_case_ids": list(CASE_IDS),
            "case_count": len(rows), "stage_count": sum(len(r["stages"]) for r in rows),
            "elapsed_seconds": time.monotonic()-started, "scope": plan["scope"], "policy_decisions": []}
        write_json(stage/"capture.json", report)
        payload_size(stage)
        return report
    except BaseException as error:
        write_json(stage/"failure.json", {"contract": CONTRACT, "state": "capture_failed_preserve_actual_attempt", "error": repr(error),
                   "traceback": traceback.format_exc(), "completed_process": process, "elapsed_seconds": time.monotonic()-started})
        raise


def main():
    reference.MAX_BUILD = BOUNDS["fresh_stage_bytes"]
    reference.MAX_OUTPUT = BOUNDS["combined_payload_bytes"]
    parser = argparse.ArgumentParser(description=__doc__)
    action_mode = parser.add_mutually_exclusive_group(required=True)
    action_mode.add_argument("--prepare", action="store_true")
    action_mode.add_argument("--capture", action="store_true")
    parser.add_argument("--stage", type=Path)
    parser.add_argument("--prepared", type=Path, default=STAGE/"prepare-001")
    args = parser.parse_args()
    stage = args.stage or STAGE/("prepare-001" if args.prepare else "capture-001")
    report = prepare(stage) if args.prepare else capture(stage, args.prepared)
    print(json.dumps({"state": report["state"], "stage": stage_path(stage).relative_to(ROOT).as_posix(),
                      "report": "prepared.json" if args.prepare else "capture.json"}))


if __name__ == "__main__":
    main()


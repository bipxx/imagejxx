"""Bounded, separately prepared original ImageJ 1.54r rank observations.

Only this observer is compiled. Original source/classes/runtime are verified by
the qualified reference supervisor. Prepare probes actual dialog/preview event
transport without a K case; capture is a separate, explicit action. Neither
action grants numerical, native, commercial, editor or OS-input acceptance.
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
STAGE = ROOT / "build/references/rank-filter-imagej154r-001"
CONTRACT = "g042.rank-filter-reference.001"
CLASS_NAME = "ij.plugin.filter.RankFilterReference"
CASE_IDS = tuple(f"K{i:02d}" for i in range(1, 17))
ENTRY = ROOT / "docs/parity/reviews/G042-rank-editor-entry-001.json"
ENTRY_SHA = "7b4e4edd133d34431f8e763fbc8db82d215d4e6e5b546402ea6203fcb43b0b02"
LINEAGE_SHA = "84e355ed4a509c1e95ae63f8736cddf3f23a68c6ce2bad0bbe91e0fb37e8f16a"
BOUNDS = {"case_count": 16, "max_pixels_per_fixture": 4096,
          "max_scalar_samples_per_case": 131072, "case_seconds": 5,
          "prepare_parent_seconds": 120, "capture_parent_seconds": 120,
          "java_heap_mib": 256, "combined_payload_bytes": 8388608,
          "fresh_stage_bytes": 33554432, "max_live_hosts": 2}
METHODS = ("minimum", "maximum", "median", "mean")
EXTRA_SOURCE_PINS = {
    "ij/gui/GenericDialog.java": "6905cc76a35f47c554d2d7ac5e6fd0b7bea6f3376d9a8af18f28a123a7051b20",
    "ij/gui/NonBlockingGenericDialog.java": "ccb7b8dfb91521d1067d0412a14f0b5845ee8e2e03cbc28916705abc529f6a2a",
    "ij/gui/GUI.java": "d30af7deca53018b0aaec7398f99d24dd47022e067d1888c0c6e57969226f512",
    "ij/Prefs.java": "5a35547eb3700613600fec474b78aa4d828e128c901dc5d401bdd6bbf06ece1c",
    "ij/Macro.java": "fab8720b69b9cd02700a90e192c9d46d411d8badaa68945623042cd9944152aa",
    "ij/process/ColorProcessor.java": "23da704d928476adf42a2d562360eaa7c8b740ad02fba2728074be0bac490650",
}


def fixture(name, dtype="uint16", width=9, height=7, *, values=None,
            shape=(1, 1, 1), position=(0, 0, 0), roi=None, formula=None):
    planes = []
    for t in range(shape[0]):
        for z in range(shape[2]):
            for c in range(shape[1]):
                samples = values if values is not None else [formula(t, c, z, y, x) if formula else 1000+31*y+7*x for y in range(height) for x in range(width)]
                if len(samples) != width*height or width*height > BOUNDS["max_pixels_per_fixture"]:
                    raise ValueError("Fixed fixture exceeds declared XY bounds")
                fmt = {"uint8": "B", "uint16": "H", "float32": "I", "rgb24": "I"}[dtype]
                raw = b"".join(struct.pack("<f", v) if dtype == "float32" and type(v) is float else struct.pack("<"+fmt, v) for v in samples)
                planes.append({"t": t, "c": c, "z": z, "bits_hex": raw.hex()})
    return {"name": name, "dtype": dtype, "shape_tczyx": [*shape, height, width],
            "position_tcz": list(position), "roi": roi, "planes": planes}


def run_spec(name, spec, method, radius, *, dispatch="api", stack=False):
    return {"name": name, "fixture": spec, "method": method, "radius": radius,
            "dispatch": dispatch, "all_stack": stack}


def cases():
    rows = [{"id": "K01", "kind": "kernels", "radii": [0, .5, 1, 1.5, 1.75, 2, 2.5, 2.85, 3, 10], "runs": []}]
    def add(number, runs, kind="runs", **fields):
        rows.append({"id": f"K{number:02d}", "kind": kind, "runs": runs, **fields})
    for number, method in enumerate(METHODS, 2):
        runs = []
        for dtype, center in (("uint8", 200), ("uint16", 60000), ("float32", -17.5)):
            values = [float(11+7*y+3*x) if dtype == "float32" else 11+7*y+3*x for y in range(7) for x in range(9)]
            values[0] = 0. if dtype == "float32" else 0
            values[31] = center
            runs.append(run_spec(dtype, fixture(dtype, dtype, values=values), method, 1))
        add(number, runs)
    values = [1000+31*y+7*x for y in range(7) for x in range(9)]; values[31] = 60000
    add(6, [run_spec("radius."+str(r), fixture("fractional", values=values), "median", r) for r in (0, .5, 1.5, 1.75, 2, 2.5, 2.85, 3)])
    add(7, [run_spec("integer_rounding", fixture("rounding", width=25, height=25,
            values=[60001 if x < 12 or (x == 12 and y < 12) else 60000 for y in range(25) for x in range(25)]), "mean", 10)])
    add(8, [run_spec("incremental_cancellation", fixture("cancellation", "float32", 25, 25,
            values=[[0x60ad78ec, 0xe0ad78ec, 0x3f800000, 0xbf800000][i % 4] for i in range(625)]), "mean", 3)])
    add(9, [run_spec(f"{dtype}.{w}x{h}.{method}.{r}", fixture("thin", dtype, w, h,
            values=[float(i+1) if dtype == "float32" else i+1 for i in range(w*h)]), method, r)
            for dtype in ("uint8", "uint16", "float32") for w, h in ((1, 1), (1, 7), (7, 1)) for method in METHODS for r in (0, 1, 2)])
    area = {"kind": "rectangle", "xywh": [1, 1, 7, 5]}
    hole = {"kind": "holed_area", "outer_xywh": [1, 1, 7, 5], "hole_xywh": [3, 2, 3, 3]}
    add(10, [run_spec(f"{name}.{method}", fixture(name, roi=roi), method, 1, dispatch="runner") for name, roi in (("rectangle", area), ("hole", hole)) for method in METHODS])
    add(11, [run_spec(f"{name}.{method}", fixture(name, roi=roi), method, 1, dispatch="runner") for name, roi in (("absent", None), ("point", {"kind": "point", "xy": [2, 2]}), ("line", {"kind": "line", "xyxy": [1, 1, 7, 5]})) for method in METHODS])
    rgb = [(11+7*y+3*x) << 16 | (200-5*y-2*x) << 8 | (13*x+9*y) for y in range(7) for x in range(9)]
    add(12, [run_spec(method, fixture("rgb", "rgb24", values=rgb), method, 1) for method in METHODS])
    sequences = (("mixed_qnan_zero", [0x7fc12345, 0xffc23456, 0x80000000, 0, 0x3f800000, 0xbf800000, 0x40000000]),
                 ("all_qnan", [0x7fc12345]), ("reference_only_infinity", [0x7fc12345, 0xff800000, 0x7f800000, 0x80000000, 0, 0x3f800000, 0xbf800000]))
    add(13, [run_spec(name+"."+method, fixture(name, "float32", values=[seq[i % len(seq)] for i in range(63)]), method, 1) for name, seq in sequences for method in METHODS])
    add(14, [run_spec(scope+"."+method, fixture("hyperstack", shape=(2, 3, 4), position=(1, 2, 3),
            formula=lambda t, c, z, y, x: 1000+100*t+30*c+5*z+2*y+x), method, 1, dispatch="runner", stack=scope == "all_stack")
            for scope in ("current", "all_stack") for method in METHODS])
    add(15, [], kind="dialogs", methods=list(METHODS), fixture=fixture("dialog"),
        timeline=[{"label": "fresh.accept2", "radius": 2, "accept": True},
                  {"label": "reopen.cancel3", "radius": 3, "accept": False},
                  {"label": "reopen.after_cancel", "radius": None, "accept": False}])
    add(16, [], kind="preview", fixture=fixture("preview", roi=hole), method="median",
        timeline=[{"label": "preview.enable", "operation": "preview", "value": True},
                  {"label": "preview.radius3", "operation": "radius", "value": 3},
                  {"label": "preview.disable", "operation": "preview", "value": False},
                  {"label": "preview.cancel", "operation": "cancel"},
                  {"label": "fresh.accept2", "operation": "fresh_accept", "radius": 2}])
    if tuple(row["id"] for row in rows) != CASE_IDS:
        raise ValueError("Fixed K01--K16 membership differs")
    for row in rows:
        samples = sum(len(p["bits_hex"])//(2*{"uint8": 1, "uint16": 2, "float32": 4, "rgb24": 4}[r["fixture"]["dtype"]])*(3 if r["fixture"]["dtype"] == "rgb24" else 1) for r in row["runs"] for p in r["fixture"]["planes"])
        if samples > BOUNDS["max_scalar_samples_per_case"]:
            raise ValueError("Fixed case exceeds scalar bound")
    return rows


DRIVER = r'''package ij.plugin.filter;
import ij.*;
import ij.gui.*;
import ij.process.*;
import java.awt.*;
import java.awt.event.*;
import java.lang.reflect.*;
import java.nio.*;
import java.util.*;
import java.util.List;
import java.util.concurrent.*;

public final class RankFilterReference {
 static final String CONTRACT="g042.rank-filter-reference.001";
 static final List<Object> CASES=caseInputs();
__FIXTURE_METHODS__
 static ImageJ app; static long deadline; static int liveDialogs;
 static Map<String,Object> o(Object...p){Map<String,Object> m=new LinkedHashMap<>();for(int i=0;i<p.length;i+=2)m.put((String)p[i],p[i+1]);return m;}
 static List<Object> a(Object...v){return new ArrayList<>(Arrays.asList(v));}
 @SuppressWarnings("unchecked") static Map<String,Object> m(Object v){return (Map<String,Object>)v;}
 @SuppressWarnings("unchecked") static List<Object> l(Object v){return (List<Object>)v;}
 static int n(Object v){return ((Number)v).intValue();} static double d(Object v){return ((Number)v).doubleValue();}
 static int id(Object v){return v==null?0:System.identityHashCode(v);}
 static String quote(String s){StringBuilder b=new StringBuilder("\"");for(int i=0;i<s.length();i++){char c=s.charAt(i);if(c=='"'||c=='\\')b.append('\\').append(c);else if(c<32||c>127)b.append(String.format("\\u%04x",(int)c));else b.append(c);}return b.append('"').toString();}
 static String json(Object v){if(v==null)return "null";if(v instanceof String)return quote((String)v);if(v instanceof Number){if(v instanceof Double&&!Double.isFinite((Double)v))throw new IllegalArgumentException("Unencoded nonfinite");return v.toString();}if(v instanceof Boolean)return v.toString();StringBuilder b=new StringBuilder();if(v instanceof Map){b.append('{');boolean first=true;for(Object raw:((Map<?,?>)v).entrySet()){Map.Entry<?,?>e=(Map.Entry<?,?>)raw;if(!first)b.append(',');first=false;b.append(quote((String)e.getKey())).append(':').append(json(e.getValue()));}return b.append('}').toString();}b.append('[');boolean first=true;if(v instanceof Iterable){for(Object x:(Iterable<?>)v){if(!first)b.append(',');first=false;b.append(json(x));}}else if(v.getClass().isArray()){for(int i=0;i<Array.getLength(v);i++){if(i>0)b.append(',');b.append(json(Array.get(v,i)));}}else throw new IllegalArgumentException("Unknown observation "+v.getClass());return b.append(']').toString();}
 static Object field(Class<?> c,Object owner,String name){try{Field f=c.getDeclaredField(name);f.setAccessible(true);return f.get(owner);}catch(Exception e){throw new IllegalStateException("Cannot observe "+c.getName()+"."+name,e);}}
 static String hex(byte[] b){char[]digits="0123456789abcdef".toCharArray(),out=new char[2*b.length];for(int i=0;i<b.length;i++){out[2*i]=digits[(b[i]&255)>>>4];out[2*i+1]=digits[b[i]&15];}return new String(out);}
 static byte[] bytes(String s){byte[] b=new byte[s.length()/2];for(int i=0;i<b.length;i++)b[i]=(byte)Integer.parseInt(s.substring(2*i,2*i+2),16);return b;}
 static String bits(Object pixels){int size=pixels instanceof byte[]?1:pixels instanceof short[]?2:4;ByteBuffer b=ByteBuffer.allocate(Array.getLength(pixels)*size).order(ByteOrder.LITTLE_ENDIAN);for(int i=0;i<Array.getLength(pixels);i++){if(pixels instanceof byte[])b.put(((byte[])pixels)[i]);else if(pixels instanceof short[])b.putShort(((short[])pixels)[i]);else if(pixels instanceof float[])b.putInt(Float.floatToRawIntBits(((float[])pixels)[i]));else b.putInt(((int[])pixels)[i]);}return hex(b.array());}
 static ImageProcessor processor(String dtype,int w,int h,String raw){ByteBuffer b=ByteBuffer.wrap(bytes(raw)).order(ByteOrder.LITTLE_ENDIAN);if(dtype.equals("uint8"))return new ByteProcessor(w,h,b.array(),null);if(dtype.equals("uint16")){short[] p=new short[w*h];for(int i=0;i<p.length;i++)p[i]=b.getShort();return new ShortProcessor(w,h,p,null);}if(dtype.equals("float32")){float[] p=new float[w*h];for(int i=0;i<p.length;i++)p[i]=Float.intBitsToFloat(b.getInt());return new FloatProcessor(w,h,p);}int[] p=new int[w*h];for(int i=0;i<p.length;i++)p[i]=b.getInt();return new ColorProcessor(w,h,p);}
 static Object rect(Rectangle r){return a(r.x,r.y,r.width,r.height);}
 static Roi roi(Object raw){if(raw==null)return null;Map<String,Object>s=m(raw);String kind=(String)s.get("kind");if(kind.equals("rectangle")){List<Object>v=l(s.get("xywh"));return new Roi(n(v.get(0)),n(v.get(1)),n(v.get(2)),n(v.get(3)));}if(kind.equals("point")){List<Object>v=l(s.get("xy"));return new PointRoi(n(v.get(0)),n(v.get(1)));}if(kind.equals("line")){List<Object>v=l(s.get("xyxy"));return new Line(n(v.get(0)),n(v.get(1)),n(v.get(2)),n(v.get(3)));}List<Object>v=l(s.get("outer_xywh")),h=l(s.get("hole_xywh"));return new ShapeRoi(new Roi(n(v.get(0)),n(v.get(1)),n(v.get(2)),n(v.get(3)))).not(new ShapeRoi(new Roi(n(h.get(0)),n(h.get(1)),n(h.get(2)),n(h.get(3)))));}
 static Object roiInfo(Roi r){return r==null?null:o("identity",id(r),"type",r.getType(),"area",r.isArea(),"bounds_xywh",rect(r.getBounds()),"mask_bits_hex",r.getMask()==null?null:bits(r.getMask().getPixels()));}
 static ImagePlus build(Map<String,Object>s){List<Object>shape=l(s.get("shape_tczyx"));int w=n(shape.get(4)),h=n(shape.get(3));ImageStack stack=new ImageStack(w,h);for(Object p:l(s.get("planes")))stack.addSlice(processor((String)s.get("dtype"),w,h,(String)m(p).get("bits_hex")));ImagePlus image=new ImagePlus((String)s.get("name"),stack);image.setDimensions(n(shape.get(1)),n(shape.get(2)),n(shape.get(0)));image.setOpenAsHyperStack(stack.size()>1);List<Object>pos=l(s.get("position_tcz"));image.setPosition(n(pos.get(1))+1,n(pos.get(2))+1,n(pos.get(0))+1);image.setRoi(roi(s.get("roi")),false);return image;}
 static Object image(ImagePlus im){List<Object>planes=new ArrayList<>();for(int i=1;i<=im.getStackSize();i++){int[]czt=im.convertIndexToPosition(i);planes.add(o("stack_index_1",i,"t",czt[2]-1,"c",czt[0]-1,"z",czt[1]-1,"bits_hex",bits(im.getStack().getPixels(i))));}return o("identity",id(im),"image_id",im.getID(),"shape_tczyx",a(im.getNFrames(),im.getNChannels(),im.getNSlices(),im.getHeight(),im.getWidth()),"position_tcz",a(im.getFrame()-1,im.getChannel()-1,im.getSlice()-1),"current_stack_index_1",im.getCurrentSlice(),"planes",planes,"roi",roiInfo(im.getRoi()),"locked",im.isLocked(),"changes",im.changes,"image_window_present",im.getWindow()!=null);}
 static int type(String name){return name.equals("minimum")?RankFilters.MIN:name.equals("maximum")?RankFilters.MAX:name.equals("median")?RankFilters.MEDIAN:RankFilters.MEAN;}
 static String arg(String name){return name.equals("minimum")?"min":name.equals("maximum")?"max":name;}
 static final class ObservedRank extends RankFilters {
  final List<Object> calls=Collections.synchronizedList(new ArrayList<>()),listeners=Collections.synchronizedList(new ArrayList<>()); volatile PlugInFilterRunner runner;volatile int returnedFlags=-1;
  int[] kernel(double r){return super.makeLineRadii(r);}
  @Override public int showDialog(ImagePlus im,String command,PlugInFilterRunner p){runner=p;int flags=super.showDialog(im,command,p);returnedFlags=flags;return flags;}
  @Override public boolean dialogItemChanged(GenericDialog gd,AWTEvent e){boolean result=super.dialogItemChanged(gd,e);listeners.add(o("event_identity",id(e),"event_id",e==null?null:e.getID(),"source_identity",e==null?null:id(e.getSource()),"source_class",e==null?null:e.getSource().getClass().getName(),"event_dispatch_thread",EventQueue.isDispatchThread(),"super_returned",true,"valid",result,"radius",field(RankFilters.class,this,"radius")));return result;}
  @Override public void run(ImageProcessor ip){Object before=bits(ip.getPixels()),r=rect(ip.getRoi()),mask=ip.getMask()==null?null:bits(ip.getMask().getPixels()),radius=field(RankFilters.class,this,"radius"),previewing=field(RankFilters.class,this,"previewing");List<Object>stack=new ArrayList<>();for(StackTraceElement f:Thread.currentThread().getStackTrace())if(f.getClassName().startsWith("ij.plugin.filter."))stack.add(f.toString());super.run(ip);calls.add(o("before_bits_hex",before,"after_bits_hex",bits(ip.getPixels()),"processor_roi_xywh",r,"mask_bits_hex",mask,"radius",radius,"previewing",previewing,"stack_index_1",runner==null?null:runner.getSliceNumber(),"thread_identity",id(Thread.currentThread()),"thread_name",Thread.currentThread().getName(),"event_dispatch_thread",EventQueue.isDispatchThread(),"super_returned",true,"run_declaring_class","ij.plugin.filter.RankFilters","callers",stack));}
 }
 static <T>T edt(Callable<T> work)throws Exception{if(EventQueue.isDispatchThread())return work.call();FutureTask<T> task=new FutureTask<>(work);EventQueue.invokeLater(task);return task.get(remaining(700),TimeUnit.MILLISECONDS);}
 static long remaining(long cap){long n=TimeUnit.NANOSECONDS.toMillis(deadline-System.nanoTime());if(n<1)throw new IllegalStateException("Fixed case deadline exceeded");return Math.min(cap,n);}
 static void check(){remaining(1);}
 static Object kernel(double radius){int[] k=new ObservedRank().kernel(radius);int h=(k.length-2)/2;List<Object>offsets=new ArrayList<>();for(int y=0;y<h;y++)for(int x=k[2*y];x<=k[2*y+1];x++)offsets.add(a(x,y-h/2));return o("radius",radius,"api","RankFilters.makeLineRadii(double)","line_radii",k,"point_count",k[k.length-2],"radius_xy",a(k[k.length-1],h/2),"offsets_xy",offsets);}
 static Object numerical(Map<String,Object>s)throws Exception{check();ImagePlus im=build(m(s.get("fixture")));Object before=image(im);Roi retained=im.getRoi();ObservedRank f=new ObservedRank();String method=(String)s.get("method");String options=null;if(s.get("dispatch").equals("api")){f.rank(im.getProcessor(),d(s.get("radius")),type(method));}else{Thread thread=Thread.currentThread();String name=thread.getName();options="radius="+s.get("radius")+((Boolean)s.get("all_stack")?" stack":" slice");try{thread.setName("Run$_RankReference");WindowManager.setTempCurrentImage(im);Macro.setOptions(options);new PlugInFilterRunner(f,"Rank reference "+method,arg(method));}finally{Macro.setOptions((String)null);WindowManager.setTempCurrentImage(null);thread.setName(name);}}Object out=o("name",s.get("name"),"input",s,"before",before,"after",image(im),"selection_identity_preserved",im.getRoi()==retained,"dispatch",s.get("dispatch"),"macro_options",options,"returned_flags",f.returnedFlags,"calls",new ArrayList<>(f.calls),"dialog_listener_calls",new ArrayList<>(f.listeners),"kernel",kernel(d(s.get("radius"))));im.flush();return out;}
 static final class Session {final ImagePlus im;final ObservedRank filter=new ObservedRank();final Thread worker;volatile Throwable error;volatile boolean complete;GenericDialog dialog;Session(Map<String,Object>s,String method)throws Exception{im=build(s);worker=new Thread(()->{try{WindowManager.setTempCurrentImage(im);new PlugInFilterRunner(filter,"Rank reference interactive "+method,arg(method));complete=true;}catch(Throwable e){error=e;}finally{WindowManager.setTempCurrentImage(null);}},"RankReference.interactive");worker.start();long until=System.nanoTime()+TimeUnit.MILLISECONDS.toNanos(remaining(500));while(System.nanoTime()<until){if(error!=null)throw new IllegalStateException("Original runner failed",error);GenericDialog g=edt(()->GenericDialog.getInstance());if(g!=null&&g.isVisible()&&g.getTitle().equals("Rank reference interactive "+method)&&g.getNumericFields()!=null&&g.getButtons()[0]!=null){dialog=g;liveDialogs++;if(liveDialogs>2)throw new IllegalStateException("Host bound exceeded");return;}Thread.sleep(1);}throw new IllegalStateException("Original dialog did not become ready within500ms");}}
 static List<Object> classes(Object[] objects){List<Object>out=new ArrayList<>();for(Object x:objects)out.add(x.getClass().getName());return out;}
 static Object dialogState(Session s){GenericDialog g=s.dialog;TextField tf=(TextField)g.getNumericFields().get(0);Checkbox cb=g.getPreviewCheckbox();Vector<?> dl=(Vector<?>)field(GenericDialog.class,g,"dialogListeners");List<Object>listeners=new ArrayList<>();for(Object x:dl)listeners.add(o("class",x.getClass().getName(),"identity",id(x)));return o("identity",id(g),"class",g.getClass().getName(),"visible",g.isVisible(),"displayable",g.isDisplayable(),"radius_text",tf.getText(),"radius_field_identity",id(tf),"text_listeners",classes(tf.getTextListeners()),"preview_identity",id(cb),"preview_on",cb.getState(),"preview_item_listeners",classes(cb.getItemListeners()),"dialog_listeners",listeners,"filter_identity",id(s.filter),"runner_identity",id(s.filter.runner),"preview_thread",previewThread(s),"image",image(s.im));}
 static Object previewThread(Session s){Thread t=(Thread)field(PlugInFilterRunner.class,s.filter.runner,"previewThread");return t==null?null:o("identity",id(t),"name",t.getName(),"state",t.getState().toString(),"alive",t.isAlive());}
 static Object idlePreview(Session s,int minimum)throws Exception{long until=System.nanoTime()+TimeUnit.MILLISECONDS.toNanos(remaining(500));while(System.nanoTime()<until){Object observed=edt(()->{synchronized(s.filter.runner){Thread t=(Thread)field(PlugInFilterRunner.class,s.filter.runner,"previewThread");return t!=null&&t.isAlive()&&t.getState()==Thread.State.WAITING&&s.filter.calls.size()>=minimum?dialogState(s):null;}});if(observed!=null)return observed;if(s.error!=null)throw new IllegalStateException("Runner error",s.error);Thread.sleep(1);}throw new IllegalStateException("Original preview did not acknowledge and reach WAITING within500ms");}
 static Object radius(Session s,double value)throws Exception{return edt(()->{TextField tf=(TextField)s.dialog.getNumericFields().get(0);Object before=dialogState(s);tf.setText(Double.toString(value));TextEvent e=new TextEvent(tf,TextEvent.TEXT_VALUE_CHANGED);tf.dispatchEvent(e);return o("kind","radius","requested",value,"event_identity",id(e),"event_id",e.getID(),"event_source_identity",id(tf),"event_dispatch_thread",EventQueue.isDispatchThread(),"listeners",classes(tf.getTextListeners()),"before",before,"after_listener",dialogState(s));});}
 static Object preview(Session s,boolean value)throws Exception{return edt(()->{Checkbox cb=s.dialog.getPreviewCheckbox();Object before=dialogState(s);cb.setState(value);ItemEvent e=new ItemEvent(cb,ItemEvent.ITEM_STATE_CHANGED,cb.getLabel(),value?ItemEvent.SELECTED:ItemEvent.DESELECTED);cb.dispatchEvent(e);return o("kind","preview","requested",value,"event_identity",id(e),"event_id",e.getID(),"event_source_identity",id(cb),"event_dispatch_thread",EventQueue.isDispatchThread(),"listeners",classes(cb.getItemListeners()),"before",before,"after_listener",dialogState(s));});}
 static Object finish(Session s,boolean accept)throws Exception{Object delivered=edt(()->{Button b=s.dialog.getButtons()[accept?0:1];Object before=dialogState(s);ActionEvent e=new ActionEvent(b,ActionEvent.ACTION_PERFORMED,b.getActionCommand());List<Object>listeners=classes(b.getActionListeners());b.dispatchEvent(e);return o("kind",accept?"accept":"cancel","button_index",accept?0:1,"button_label",b.getLabel(),"event_identity",id(e),"event_id",e.getID(),"event_source_identity",id(b),"event_dispatch_thread",EventQueue.isDispatchThread(),"listeners",listeners,"before",before);});s.worker.join(remaining(500));if(s.worker.isAlive()||s.error!=null||!s.complete)throw new IllegalStateException("Original runner did not finish within500ms",s.error);liveDialogs--;return o("event",delivered,"worker_joined",!s.worker.isAlive(),"complete",s.complete,"returned_flags",s.filter.returnedFlags,"dialog_visible_after",s.dialog.isVisible(),"preview_thread_after",previewThread(s),"after",image(s.im),"calls",new ArrayList<>(s.filter.calls),"dialog_listener_calls",new ArrayList<>(s.filter.listeners));}
 static Object memory(String method){double[]v=(double[])field(RankFilters.class,null,"lastRadius");return v[type(method)];}
 static Object dialogs(Map<String,Object>spec)throws Exception{List<Object>out=new ArrayList<>();for(Object raw: l(spec.get("methods"))){String method=(String)raw;for(Object input:l(spec.get("timeline"))){Map<String,Object>step=m(input);Session s=new Session(m(spec.get("fixture")),method);Object before=edt(()->dialogState(s)),mem=memory(method),event=null;if(step.get("radius")!=null)event=radius(s,d(step.get("radius")));Object done=finish(s,(Boolean)step.get("accept"));out.add(o("method",method,"input",step,"before",before,"memory_before",mem,"radius_event",event,"finish",done,"memory_after",memory(method)));s.im.flush();}}return out;}
 static Object previews(Map<String,Object>spec)throws Exception{Session s=new Session(m(spec.get("fixture")),(String)spec.get("method"));Object initial=edtState(s);List<Object>out=new ArrayList<>();for(Object raw:l(spec.get("timeline"))){Map<String,Object>step=m(raw);String op=(String)step.get("operation");Object event,after;if(op.equals("fresh_accept")){s=new Session(m(spec.get("fixture")),(String)spec.get("method"));Object fresh=edtState(s);Object radius=radius(s,d(step.get("radius")));event=o("fresh_before",fresh,"radius_event",radius,"finish",finish(s,true));after=image(s.im);s.im.flush();}else if(op.equals("cancel")){event=finish(s,false);after=image(s.im);s.im.flush();}else {int count=s.filter.calls.size();event=op.equals("radius")?radius(s,d(step.get("value"))):preview(s,(Boolean)step.get("value"));after=op.equals("radius")||Boolean.TRUE.equals(step.get("value"))?idlePreview(s,count+1):edtState(s);}out.add(o("input",step,"event",event,"after",after));}return o("initial",initial,"stages",out);}
 static Object edtState(Session s)throws Exception{return edt(()->dialogState(s));}
 static Object environment(){return o("imagej_version",IJ.getVersion(),"java_version",System.getProperty("java.version"),"java_vendor",System.getProperty("java.vendor"),"os_name",System.getProperty("os.name"),"awt_headless",GraphicsEnvironment.isHeadless(),"threads",Prefs.getThreads(),"live_reference_hosts",liveDialogs,"host_mode","original_unhosted_ImagePlus_and_original_AWT_dialog","imagej_visible",app.isVisible(),"imagej_showing",app.isShowing());}
 static Object capture(Map<String,Object>spec)throws Exception{long start=System.nanoTime();deadline=start+TimeUnit.SECONDS.toNanos(5);Object result;String kind=(String)spec.get("kind");if(kind.equals("kernels")){List<Object>kernels=new ArrayList<>();for(Object r:l(spec.get("radii")))kernels.add(kernel(d(r)));result=kernels;}else if(kind.equals("dialogs"))result=dialogs(spec);else if(kind.equals("preview"))result=previews(spec);else{List<Object>runs=new ArrayList<>();for(Object raw:l(spec.get("runs")))runs.add(numerical(m(raw)));result=runs;}check();return o("contract",CONTRACT,"id",spec.get("id"),"input_contract",spec,"observations",result,"environment",environment(),"elapsed_ms",(System.nanoTime()-start)/1000000.0);}
 static Object probe()throws Exception{long start=System.nanoTime();deadline=start+TimeUnit.SECONDS.toNanos(5);Map<String,Object>f=o("name","environment_probe_not_K","dtype","uint16","shape_tczyx",a(1,1,1,2,2),"position_tcz",a(0,0,0),"roi",null,"planes",a(o("t",0,"c",0,"z",0,"bits_hex","0100020003000400")));Session s=new Session(f,"mean");Object before=edtState(s),on=preview(s,true),ready=idlePreview(s,1),off=preview(s,false),done=finish(s,false);s.im.flush();return o("contract",CONTRACT,"kind","environment_probe_no_K_cases","before",before,"preview_on",on,"preview_ready",ready,"preview_off",off,"cleanup",done,"environment",environment(),"elapsed_ms",(System.nanoTime()-start)/1000000.0);}
 public static void main(String[]args){try{deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(5);edt(()->{app=new ImageJ(ImageJ.NO_SHOW);Prefs.setGuiScale(1);Prefs.setThreads(1);Prefs.autoContrast=false;Prefs.useInvertingLut=false;return null;});if(args.length!=1)throw new IllegalArgumentException("Expected --probe or --capture");if(args[0].equals("--probe"))System.out.println(json(probe()));else if(args[0].equals("--capture")){long start=System.nanoTime();for(Object raw:CASES){if(System.nanoTime()-start>TimeUnit.SECONDS.toNanos(85))throw new IllegalStateException("Java loop exceeded85s");System.out.println(json(capture(m(raw))));System.out.flush();}}else throw new IllegalArgumentException("Unknown action");deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(2);edt(()->{WindowManager.setTempCurrentImage(null);app.dispose();return null;});System.exit(0);}catch(Throwable error){error.printStackTrace(System.err);System.exit(1);}}
}
'''


def driver_source():
    # Keep each Java initializer bounded even for K09's108 runs and K14's24 planes.
    methods, case_calls = [], []
    for spec in cases():
        name = "case"+spec["id"]
        case_calls.append(name+"()")
        run_calls = []
        for number, run in enumerate(spec["runs"], 1):
            run_name = name+f"Run{number:03d}"
            run_calls.append(run_name+"()")
            methods.append(" static Object "+run_name+"(){return "+lineage.java_value(run)+";}\n")
        fields = []
        for key, value in spec.items():
            fields.extend((lineage.java_value(key), "a("+",".join(run_calls)+")" if key == "runs" else lineage.java_value(value)))
        methods.append(" static Object "+name+"(){return o("+",".join(fields)+");}\n")
    methods.insert(0, " static List<Object> caseInputs(){return a("+",".join(case_calls)+");}\n")
    return DRIVER.replace("__FIXTURE_METHODS__", "".join(methods))


def input_plan(verified):
    return {"contract": CONTRACT, "ordered_case_ids": list(CASE_IDS), "bounds": BOUNDS,
            "cases": cases(), "reference": verified["reference"],
            "environment_inputs": {"ImageJ_constructor": "ImageJ.NO_SHOW", "gui_scale": 1,
                "threads": 1, "image_host": "unhosted original ImagePlus", "dialogs": "original AWT GenericDialog; programmatic real component events on EDT",
                "preview": "original PlugInFilterRunner preview thread, acknowledged WAITING then readback", "reflection": "read-only state observations"},
            "scope": "original numerical API and actual runner/dialog/preview observations only; no product/OS-input/commercial acceptance"}


def stage_path(path):
    path = reference._reference_path(path)
    if path == STAGE.resolve() or not path.is_relative_to(STAGE.resolve()):
        raise ValueError("Use a fresh child of the owned rank reference stage")
    return path


def write_json(path, value):
    raw = (json.dumps(value, indent=2, ensure_ascii=True, allow_nan=False)+"\n").encode()
    if len(raw) > BOUNDS["combined_payload_bytes"]:
        raise ValueError("JSON exceeds8MiB")
    with path.open("xb") as stream:
        stream.write(raw)


def payload_size(stage):
    total = sum(p.stat().st_size for p in stage.rglob("*") if p.is_file() and p.suffix in (".json", ".stdout", ".stderr"))
    if total > BOUNDS["combined_payload_bytes"]:
        raise ValueError("Combined payload exceeds8MiB")
    reference._storage(stage)
    return total


def inputs(deadline):
    if reference.digest(ENTRY) != ENTRY_SHA or reference.digest(Path(lineage.__file__).resolve()) != LINEAGE_SHA:
        raise ValueError("Held rank entry or qualified supervisor differs")
    verified = lineage.inputs(deadline)
    entry = json.loads(ENTRY.read_text(encoding="utf-8"))
    prefix = "ImageJ-"+reference.COMMIT+"/"
    sources = {row["member"].removeprefix(prefix): {"bytes": row["bytes"], "sha256": row["sha256"]} for row in entry["reference"]["source_members"]}
    for name, sha in EXTRA_SOURCE_PINS.items():
        sources[name] = {"bytes": (reference.REUSE/"sources"/name).stat().st_size, "sha256": sha}
    for name, expected in sources.items():
        path = reference.REUSE/"sources"/name
        if path.stat().st_size != expected["bytes"] or reference.digest(path) != expected["sha256"]:
            raise ValueError("Pinned behavioral source differs: "+name)
    for name in ("ij/plugin/filter/RankFilters.class", "ij/plugin/filter/PlugInFilterRunner.class", "ij/gui/GenericDialog.class", "ij/process/ColorProcessor.class"):
        if not (lineage.REUSED_CLASSES/name).is_file():
            raise ValueError("Qualified original class absent: "+name)
    verified.update({"rank_entry": lineage.pin(ENTRY), "rank_source_members": sources,
                     "qualified_supervisor_source": lineage.pin(Path(lineage.__file__).resolve())})
    return verified


def begin(stage, mode):
    if stage.exists():
        raise ValueError("Stage must be fresh; preserve every prior attempt")
    stage.mkdir(parents=True)
    for name in ("home", "tmp", "tool-sources", "empty-sourcepath"):
        (stage/name).mkdir()
    for tool in (Path(__file__).resolve(), Path(lineage.__file__).resolve(), Path(reference.__file__).resolve(), ROOT/"scripts/capture_binary_reference.py"):
        shutil.copyfile(tool, stage/"tool-sources"/tool.name)
    write_json(stage/"attempt.json", {"contract": CONTRACT, "action": mode, "harness": lineage.pin(Path(__file__).resolve()),
               "state": "attempt_started_no_acceptance", "ordered_case_ids": list(CASE_IDS), "bounds": BOUNDS})


def run(argv, stage, label, seconds, deadline):
    seconds = min(seconds, int(deadline-time.monotonic()-2))
    if seconds < 1:
        raise TimeoutError("No remaining parent budget")
    write_json(stage/(label+".command.json"), {"argv": argv, "timeout_seconds": seconds,
               "parent_remaining_seconds": deadline-time.monotonic(), "scope": "reference only; no OS input or product acceptance"})
    reference.MAX_OUTPUT = BOUNDS["combined_payload_bytes"]-sum(p.stat().st_size for p in stage.rglob("*") if p.is_file() and p.suffix in (".json", ".stdout", ".stderr"))-65536
    if reference.MAX_OUTPUT < 1:
        raise ValueError("No bounded raw-output space remains")
    return reference._run(argv, stage, label, seconds)


def java_command(stage, prepared, mode):
    return [str(reference.REUSE/"jdk/bin/java.exe"), "-Xmx256m", "-Djava.awt.headless=false",
            "-Duser.home="+str(stage/"home"), "-Djava.io.tmpdir="+str(stage/"tmp"), "-Dij.dir="+str(stage),
            "-cp", os.pathsep.join((str(prepared/"classes"), str(lineage.REUSED_CLASSES))), CLASS_NAME, mode]


def prepare(stage):
    stage = stage_path(stage); deadline = time.monotonic()+120
    begin(stage, "prepare"); processes = {}
    try:
        verified = inputs(deadline); plan = input_plan(verified)
        write_json(stage/"verified-inputs.json", verified); write_json(stage/"input-plan.json", plan)
        driver = stage/"RankFilterReference.java"
        driver.write_bytes(driver_source().encode())
        (stage/"classes").mkdir()
        write_json(stage/"execution-inputs.json", {"driver": lineage.pin(driver), "harness": lineage.pin(Path(__file__).resolve()),
            "input_plan": lineage.pin(stage/"input-plan.json"), "verified_inputs": lineage.pin(stage/"verified-inputs.json"),
            "tools": [lineage.pin(p) for p in sorted((stage/"tool-sources").iterdir())]})
        processes["compile"] = run([str(reference.REUSE/"jdk/bin/javac.exe"), "-J-Xmx256m", "-encoding", "UTF-8", "-proc:none", "-implicit:none",
            "-sourcepath", str(stage/"empty-sourcepath"), "-classpath", str(lineage.REUSED_CLASSES), "-d", str(stage/"classes"), str(driver)], stage, "javac", 60, deadline)
        processes["version"] = run([str(reference.REUSE/"jdk/bin/java.exe"), "-Xmx128m", "-version"], stage, "version", 10, deadline)
        processes["probe"] = run(java_command(stage, stage, "--probe"), stage, "probe", 15, deadline)
        probe = json.loads((stage/"probe.stdout").read_text(encoding="utf-8"))
        if probe["contract"] != CONTRACT or probe["kind"] != "environment_probe_no_K_cases" or probe["elapsed_ms"] > 5000:
            raise ValueError("Wrong environment-only probe/deadline")
        if (probe["environment"]["awt_headless"] or probe["environment"]["imagej_version"] != "1.54r" or probe["environment"]["threads"] != 1
                or probe["environment"]["live_reference_hosts"] != 0 or not probe["cleanup"]["worker_joined"]
                or probe["cleanup"]["preview_thread_after"] is not None or probe["cleanup"]["dialog_visible_after"]
                or probe["preview_ready"]["preview_thread"]["state"] != "WAITING"
                or not probe["preview_on"]["event_dispatch_thread"] or not probe["preview_off"]["event_dispatch_thread"]):
            raise ValueError("Actual original dialog/listener/preview feasibility failed")
        for event in (probe["preview_on"], probe["preview_off"]):
            if "ij.gui.GenericDialog" not in event["listeners"] or not any(call["event_identity"] == event["event_identity"] and call["super_returned"] for call in probe["cleanup"]["dialog_listener_calls"]):
                raise ValueError("Real component event did not reach original inherited rank handler")
        classes = reference._files(stage/"classes", BOUNDS["fresh_stage_bytes"], deadline)
        if any(not name.startswith("ij/plugin/filter/RankFilterReference") or not name.endswith(".class") for name in classes):
            raise ValueError("Only new observer classes may be compiled")
        if inputs(deadline) != verified:
            raise ValueError("Qualified inputs changed during prepare")
        report = {"contract": CONTRACT, "state": "prepared_environment_only_K01_K16_not_executed", "harness": lineage.pin(Path(__file__).resolve()),
                  "input_plan": lineage.pin(stage/"input-plan.json"), "verified_inputs": lineage.pin(stage/"verified-inputs.json"),
                  "execution_inputs": lineage.pin(stage/"execution-inputs.json"), "driver": lineage.pin(driver),
                  "prepared_classes": classes, "processes": processes, "probe": probe}
        write_json(stage/"prepared.json", report); payload_size(stage); return report
    except BaseException as error:
        write_json(stage/"failure.json", {"contract": CONTRACT, "state": "prepare_failed_no_K_credit", "error": repr(error), "traceback": traceback.format_exc(), "completed_processes": processes})
        raise


def capture(stage, prepared):
    stage, prepared = stage_path(stage), stage_path(prepared); started = time.monotonic(); deadline = started+120
    begin(stage, "capture"); process = None
    try:
        held = json.loads((prepared/"prepared.json").read_text(encoding="utf-8"))
        if held["contract"] != CONTRACT or held["state"] != "prepared_environment_only_K01_K16_not_executed" or held["harness"] != lineage.pin(Path(__file__).resolve()):
            raise ValueError("Held preparation/producer differs")
        for key, filename in (("input_plan", "input-plan.json"), ("verified_inputs", "verified-inputs.json"), ("execution_inputs", "execution-inputs.json"), ("driver", "RankFilterReference.java")):
            if held[key] != lineage.pin(prepared/filename):
                raise ValueError("Prepared input differs: "+filename)
        verified = json.loads((prepared/"verified-inputs.json").read_text(encoding="utf-8"))
        if verified != inputs(deadline) or held["prepared_classes"] != reference._files(prepared/"classes", BOUNDS["fresh_stage_bytes"], deadline):
            raise ValueError("Qualified inputs/classes changed")
        plan = json.loads((prepared/"input-plan.json").read_text(encoding="utf-8"))
        if plan != input_plan(verified):
            raise ValueError("Fixed K01--K16 input plan differs")
        shutil.copyfile(prepared/"input-plan.json", stage/"input-plan.json")
        write_json(stage/"execution-inputs.json", {"prepared": lineage.pin(prepared/"prepared.json"), "harness": held["harness"],
            "input_plan": held["input_plan"], "verified_inputs": held["verified_inputs"], "driver": held["driver"], "ordered_case_ids": list(CASE_IDS), "bounds": BOUNDS})
        argv = java_command(stage, prepared, "--capture")
        write_json(stage/"command.json", {"argv": argv, "parent_seconds": 120, "maximum_process_seconds": 90,
            "prepared": lineage.pin(prepared/"prepared.json"), "input_plan": lineage.pin(stage/"input-plan.json"), "ordered_case_ids": list(CASE_IDS)})
        process = run(argv, stage, "reference", 90, deadline)
        rows = [json.loads(line) for line in (stage/"reference.stdout").read_text(encoding="utf-8").splitlines() if line.strip()]
        if tuple(row.get("id") for row in rows) != CASE_IDS:
            raise ValueError("Exact ordered K01--K16 membership missing")
        for row, spec in zip(rows, cases()):
            if row["contract"] != CONTRACT or row["input_contract"] != spec or row["elapsed_ms"] > 5000 or row["environment"]["live_reference_hosts"] != 0:
                raise ValueError("Raw input/deadline/cleanup differs")
        if inputs(deadline) != verified or reference._files(prepared/"classes", BOUNDS["fresh_stage_bytes"], deadline) != held["prepared_classes"]:
            raise ValueError("Consumed reference inputs changed during capture")
        report = {"contract": CONTRACT, "state": "raw_observations_only_no_numerical_or_product_acceptance", "prepared": lineage.pin(prepared/"prepared.json"),
            "harness": held["harness"], "process": process, "raw": lineage.pin(stage/"reference.stdout"), "input_plan": lineage.pin(stage/"input-plan.json"),
            "execution_inputs": lineage.pin(stage/"execution-inputs.json"), "ordered_case_ids": list(CASE_IDS), "case_count": len(rows),
            "elapsed_seconds": time.monotonic()-started, "scope": plan["scope"], "policy_decisions": []}
        write_json(stage/"capture.json", report); payload_size(stage); return report
    except BaseException as error:
        write_json(stage/"failure.json", {"contract": CONTRACT, "state": "capture_failed_preserve_actual_attempt", "error": repr(error), "traceback": traceback.format_exc(), "completed_process": process, "elapsed_seconds": time.monotonic()-started})
        raise


def main():
    reference.MAX_BUILD = BOUNDS["fresh_stage_bytes"]; reference.MAX_OUTPUT = BOUNDS["combined_payload_bytes"]
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group(required=True); modes.add_argument("--prepare", action="store_true"); modes.add_argument("--capture", action="store_true")
    parser.add_argument("--stage", type=Path); parser.add_argument("--prepared", type=Path, default=STAGE/"prepare-001")
    args = parser.parse_args(); stage = args.stage or STAGE/("prepare-001" if args.prepare else "capture-001")
    report = prepare(stage) if args.prepare else capture(stage, args.prepared)
    print(json.dumps({"state": report["state"], "stage": stage_path(stage).relative_to(ROOT).as_posix(), "report": "prepared.json" if args.prepare else "capture.json"}))


if __name__ == "__main__":
    main()

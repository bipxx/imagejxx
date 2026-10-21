"""All-or-nothing geometric import, durable raw sources, no dataset reads."""
import base64
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

EXTENT=[2,3,6,8,12,1]
ROWS='imagejxx.planar_annotations'
SOURCES='imagejxx.imagej_roi_import_sources'

def owner():
    reads=ij.DatasetReadObserver()
    doc=ij.Document(ij.Dataset(np.zeros((2,3,6,8,12),dtype=np.uint16),axes='TCZYX').observe_reads(reads))
    return doc,reads

def mapping(plane='xy',coordinate=0,extent=None):
    m=ij.ImageJRoiPlaneMapping();m.extent=extent or EXTENT;m.plane=plane;m.coordinate=coordinate
    return m

def bare(name='bare.roi',plane='xy',coordinate=0):
    r=ij.ImageJRoi(ij.ROI(name,ij.Selection('rectangle',[(1.25,2.5),(3.75,4.5)]),t=0,c=1))
    r.properties='source: λ😀\n';r.group=7;r.overlay_font_size=14
    return ij.ImageJRoiImportItem(name,ij.encode_imagej_roi(r),mapping(plane,coordinate))

def envelope(name='native.roi',plane='yz',coordinate=0):
    item=bare()
    source=ij.decode_imagej_roi_mapped(item.data,item.mapping)
    selection=ij.Selection('rectangle',[(1.250000000001,2.500000000001),(3.750000000001,4.500000000001)],attributes={'future-selection':[None,2**64-1]})
    row=ij.PlanarAnnotation(name,selection,plane,coordinate,t=None,c=0,attributes={'future-row':'λ😀'})
    rebound=ij.rebind_imagej_roi_mapping(source,EXTENT,row)
    projection=ij.ImageJRoi(ij.ROI(row.name,selection.with_attributes({}),t=None,c=0,z=coordinate))
    wire=ij.encode_imagej_roi_native_envelope(rebound,projection)
    return ij.ImageJRoiImportItem(name,wire)

def archive(doc):
    return doc.snapshot().metadata['extensions'].get(SOURCES)

@pytest.mark.parametrize('mode',[ij.ImageJRoiImportMode.APPEND,ij.ImageJRoiImportMode.REPLACE])
def test_complete_publication_one_undo_and_source_archive(mode):
    doc,reads=owner()
    old=ij.PlanarAnnotation('existing',ij.Selection('point',[(0,0)]),'xy',0)
    doc.commit(ij.prepare_store_planar_annotation(doc.snapshot(),old))
    before=doc.snapshot();history=doc.undo_steps
    items=[bare(),bare('xz.roi','xz',7),envelope('yz.roi','yz',11)]
    resolved=ij.resolve_imagej_roi_import(before,items,mode)
    assert doc.snapshot().shares_data(before) and doc.undo_steps==history
    assert len(resolved.records)==3 and resolved.records[0].source_record.group==7
    assert [r.plane for r in resolved.annotations][-3:]==['xy','xz','yz']
    expected=4 if mode==ij.ImageJRoiImportMode.APPEND else 3
    assert len(resolved.annotations)==expected
    tx=ij.prepare_imagej_roi_import(resolved);doc.commit(tx)
    assert len(ij.planar_annotations(doc.snapshot()))==expected and doc.undo_steps==history+1
    assert doc.snapshot().dataset.shares_pixels(before.dataset) and doc.snapshot().selection is None
    rows=ij.planar_annotations(doc.snapshot());assert rows[-1].attributes=={'future-row':'λ😀'}
    assert rows[-1].selection.points==resolved.records[-1].annotation.selection.points
    saved=archive(doc)
    assert [r['name'] for r in saved['entries']]==[i.name for i in items]
    assert [base64.b64decode(r['data']) for r in saved['entries']]==[i.data for i in items]
    assert saved['entries'][0]['mapping']['extent']==EXTENT and saved['entries'][2]['mapping'] is None
    assert reads.counts().plane_reads==reads.counts().region_reads==0
    restored=ij.decode_session(ij.encode_session(ij.SessionCapture([doc]))).documents[0]
    after_serialization=reads.counts()
    assert archive(restored)==saved
    doc.undo();assert doc.snapshot().shares_data(before) and archive(doc) is None
    doc.redo();assert archive(doc)==saved and len(ij.planar_annotations(doc.snapshot()))==expected
    assert reads.counts().plane_reads==after_serialization.plane_reads
    assert reads.counts().region_reads==after_serialization.region_reads

@pytest.mark.parametrize('failure',['invalid-last','duplicate-label','missing-context','wrong-extent','empty','text-cap'])
def test_invalid_complete_batch_never_publishes_a_prefix(failure):
    doc,reads=owner();before=doc.snapshot()
    items=[bare(),envelope()];limits=ij.ImageJRoiImportLimits()
    if failure=='invalid-last':items[-1]=ij.ImageJRoiImportItem('bad.roi',b'Iout'+b'\0'*60)
    if failure=='duplicate-label':items[-1]=ij.ImageJRoiImportItem(items[0].name,items[-1].data)
    if failure=='missing-context':items[0]=ij.ImageJRoiImportItem(items[0].name,items[0].data)
    if failure=='wrong-extent':items[0]=ij.ImageJRoiImportItem(items[0].name,items[0].data,mapping(extent=[2,3,6,8,12,3]))
    if failure=='empty':items=[]
    if failure=='text-cap':limits=ij.ImageJRoiImportLimits(roi=ij.ImageJRoiLimits(max_text_bytes=40))
    with pytest.raises(ValueError):ij.resolve_imagej_roi_import(before,items,limits=limits)
    assert doc.snapshot().shares_data(before) and doc.undo_steps==0 and archive(doc) is None
    assert reads.counts().plane_reads==reads.counts().region_reads==0

def test_stale_commit_and_display_only_concurrency():
    doc,reads=owner();resolved=ij.resolve_imagej_roi_import(doc.snapshot(),[bare()])
    tx=ij.prepare_imagej_roi_import(resolved)
    other=doc.begin_edit();other.set_title('concurrent');doc.commit(other)
    with pytest.raises(ij.RevisionConflict):doc.commit(tx)
    assert not ij.planar_annotations(doc.snapshot()) and archive(doc) is None
    current=doc.snapshot();resolved=ij.resolve_imagej_roi_import(current,[bare()]);tx=ij.prepare_imagej_roi_import(resolved)
    display=current.display;display.t=1;doc.set_display(display);doc.commit(tx)
    assert doc.snapshot().display.t==1 and len(ij.planar_annotations(doc.snapshot()))==1
    assert reads.counts().plane_reads==0

def test_append_replace_and_archive_limits_include_prior_inputs():
    doc,_=owner();item=bare()
    doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[item])))
    before=doc.snapshot()
    for limits in [ij.ImageJRoiImportLimits(max_entries=1),ij.ImageJRoiImportLimits(max_total_bytes=len(item.data)*2-1),ij.ImageJRoiImportLimits(max_provenance_bytes=128)]:
        with pytest.raises(ValueError):ij.resolve_imagej_roi_import(before,[bare('next.roi')],limits=limits)
    assert doc.snapshot().shares_data(before)
    resolved=ij.resolve_imagej_roi_import(before,[bare('replacement.roi')],ij.ImageJRoiImportMode.REPLACE)
    doc.commit(ij.prepare_imagej_roi_import(resolved))
    assert len(ij.planar_annotations(doc.snapshot()))==1 and len(archive(doc)['entries'])==1
    doc.undo();assert archive(doc)['entries'][0]['name']=='bare.roi'

@pytest.mark.parametrize('corruption',['schema','data','mapping','unknown'])
def test_existing_provenance_is_validated_before_replacement(corruption):
    doc,reads=owner();resolved=ij.resolve_imagej_roi_import(doc.snapshot(),[bare()])
    saved=resolved.provenance()
    if corruption=='schema':saved['version']=True
    if corruption=='data':saved['entries'][0]['data']='AA=='
    if corruption=='mapping':saved['entries'][0]['mapping']['coordinate']=99
    if corruption=='unknown':saved['unexpected']=0
    tx=doc.begin_edit();tx.set_extension(SOURCES,saved);doc.commit(tx);before=doc.snapshot()
    with pytest.raises(ValueError):ij.resolve_imagej_roi_import(before,[bare()],ij.ImageJRoiImportMode.REPLACE)
    assert doc.snapshot().shares_data(before) and reads.counts().plane_reads==0

@pytest.mark.parametrize('compressed',[False,True])
def test_zip_complete_member_mapping_and_atomic_semantic_admission(compressed):
    doc,reads=owner();a=bare();b=envelope()
    zipdata=ij.encode_imagej_roi_zip([ij.ImageJRoiZipEntry(a.name,a.data),ij.ImageJRoiZipEntry(b.name,b.data)],compress=compressed)
    resolved=ij.resolve_imagej_roi_zip_import(doc.snapshot(),zipdata,[a.mapping,None])
    assert len(resolved.records)==2 and [r.name for r in resolved.annotations]==[a.name,b.name]
    for choices in ([],[a.mapping],[None,None]):
        with pytest.raises(ValueError):ij.resolve_imagej_roi_zip_import(doc.snapshot(),zipdata,choices)
    bad=ij.encode_imagej_roi_zip([ij.ImageJRoiZipEntry(a.name,a.data),ij.ImageJRoiZipEntry('bad.roi',b'Iout'+b'\0'*60)],compress=compressed)
    with pytest.raises(ValueError):ij.resolve_imagej_roi_zip_import(doc.snapshot(),bad,[a.mapping,None])
    assert doc.undo_steps==0 and reads.counts().plane_reads==0
    doc.commit(ij.prepare_imagej_roi_import(resolved))
    assert len(ij.planar_annotations(doc.snapshot()))==2 and len(archive(doc)['entries'])==2

@pytest.mark.parametrize('value',[True,-1,0,1.5,2**100])
def test_strict_bounded_limits(value):
    with pytest.raises((ValueError,TypeError,OverflowError)):ij.ImageJRoiImportLimits(max_entries=value)

def test_copied_provenance_and_row_cap():
    doc,_=owner();resolved=ij.resolve_imagej_roi_import(doc.snapshot(),[bare()])
    mutated=resolved.provenance();mutated['entries'][0]['data']='bad'
    assert resolved.provenance()['entries'][0]['data']!='bad'
    doc.commit(ij.prepare_imagej_roi_import(resolved))
    ext=doc.snapshot().metadata['extensions'][ROWS];ext['rows']*=4096
    tx=doc.begin_edit();tx.set_extension(ROWS,ext);doc.commit(tx);before=doc.snapshot()
    with pytest.raises(ValueError,match='row cap'):ij.resolve_imagej_roi_import(before,[bare('extra.roi')])
    assert doc.snapshot().shares_data(before)

def test_decoded_mask_retention_budget_is_aggregate():
    doc=ij.Document(ij.Dataset(np.zeros((1024,1024),dtype=np.uint8),axes='YX'))
    context=mapping(extent=[1,1,1,1024,1024,1])
    shape=ij.imagej_shape_mask([0,1,1,1,4,1,1,4,4,1,1,4,4],1024,1024)
    source=ij.ImageJRoi(ij.ROI('mask',shape));source.composite_path=[0,1,1,1,4,1,1,4,4,1,1,4,4]
    wire=ij.encode_imagej_roi(source)
    items=[ij.ImageJRoiImportItem('first.roi',wire,context),ij.ImageJRoiImportItem('last.roi',wire,context)]
    limits=ij.ImageJRoiImportLimits(max_decoded_bytes=500000)
    assert len(ij.resolve_imagej_roi_import(doc.snapshot(),items[:1],limits=limits).records)==1
    before=doc.snapshot()
    with pytest.raises(ValueError,match='decoded retention'):
        ij.resolve_imagej_roi_import(before,items,limits=limits)
    assert doc.snapshot().shares_data(before) and doc.undo_steps==0

def test_old_sources_are_provenance_and_never_overwrite_edited_rows():
    doc,_=owner()
    doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[bare()])))
    prior=ij.planar_annotations(doc.snapshot())[0]
    edited=ij.PlanarAnnotation('edited',prior.selection,'yz',2,attributes={'new':'native'})
    doc.commit(ij.prepare_update_planar_annotation(doc.snapshot(),0,edited))
    doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[envelope()])))
    assert ij.planar_annotations(doc.snapshot())[0].name=='edited'
    assert ij.planar_annotations(doc.snapshot())[0].attributes=={'new':'native'}
    assert len(archive(doc)['entries'])==2

def test_actual_imagej_42_record_batch_and_stale_last_member():
    fixture=json.loads((Path(__file__).parents[1]/'fixtures/imagej-roi-native-envelope-imagej154r.json').read_text())
    cases=fixture['cases'];doc,reads=owner();before=doc.snapshot()
    items=[ij.ImageJRoiImportItem(f"{case['ordinal']}.roi",bytes.fromhex(case['unchanged'])) for case in cases]
    bad=items[:-1]+[ij.ImageJRoiImportItem('bad-last.roi',bytes.fromhex(cases[-1]['edited']))]
    with pytest.raises(ValueError,match='Stale native'):
        ij.resolve_imagej_roi_import(before,bad)
    assert doc.snapshot().shares_data(before) and doc.undo_steps==0
    resolved=ij.resolve_imagej_roi_import(before,items)
    doc.commit(ij.prepare_imagej_roi_import(resolved))
    rows=ij.planar_annotations(doc.snapshot())
    assert len(rows)==len(resolved.records)==len(archive(doc)['entries'])==42
    for row,case in zip(rows,cases):
        assert row.plane==case['plane'] and row.coordinate==case['coordinate']
        assert row.selection.points==case['points']
    assert doc.undo_steps==1 and reads.counts().plane_reads==reads.counts().region_reads==0
    doc.undo();assert doc.snapshot().shares_data(before) and archive(doc) is None

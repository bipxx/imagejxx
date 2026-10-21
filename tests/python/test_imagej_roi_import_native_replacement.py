"""Original-byte-preserving envelope replacement and versioned import equations."""
import base64
import copy
import json
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop
from test_imagej_roi_import import envelope,bare,mapping,archive,EXTENT,SOURCES

TARGET=[3,2,8,10,14,1]

def target(extent=TARGET):
    reads=ij.DatasetReadObserver()
    doc=ij.Document(ij.Dataset(np.zeros(extent,dtype=np.uint16),axes='TCZYXS').observe_reads(reads))
    return doc,reads

def replacement(plane='xz',coordinate=7,extent=TARGET):
    original=ij.decode_imagej_roi_envelope(envelope().data).annotation
    row=ij.PlanarAnnotation('explicit λ😀',original.selection,plane,coordinate,t=2,c=1,attributes=original.attributes)
    return ij.ImageJRoiNativeReplacement(extent,row)

def item(name='original.roi',choice=None):
    source=envelope(name)
    return ij.ImageJRoiImportItem(name,source.data,native_replacement=choice or replacement())

@pytest.mark.parametrize('plane,coordinate',[('xy',7),('xz',9),('yz',13)])
def test_exact_original_transport_opaque_precision_and_complete_destination(plane,coordinate):
    doc,reads=target();choice=replacement(plane,coordinate);source=item(choice=choice)
    with pytest.raises(ValueError,match='explicit rebinding'):
        ij.resolve_imagej_roi_import(doc.snapshot(),[ij.ImageJRoiImportItem('unchanged.roi',source.data)])
    result=ij.resolve_imagej_roi_import(doc.snapshot(),[source]);record=result.record()
    assert record['version']==record['after_sources']['version']==2
    saved=record['after_sources']['entries'][0]
    assert saved['mapping'] is None and base64.b64decode(saved['data'])==envelope('original.roi').data==source.data
    assert saved['native_replacement']['extent']==TARGET
    assert saved['native_replacement']['annotation']['plane']==plane.upper()
    mapped=result.records[0];row=mapped.annotation
    assert mapped.target_extent==TARGET and mapped.source_mapping.extent==EXTENT
    assert mapped.source_record.group==7 and mapped.source_record.properties=='source: λ😀\n'
    assert row.selection.points==[[1.250000000001,2.500000000001],[3.750000000001,4.500000000001]]
    assert row.selection.attributes=={'future-selection':[None,2**64-1]}
    assert row.attributes=={'future-row':'λ😀'} and (row.plane,row.coordinate,row.t,row.c)==(plane,coordinate,2,1)
    request=ij.decode_imagej_roi_import_request(record);assert request.items[0].data==source.data
    assert request.items[0].native_replacement.extent==TARGET
    assert request.items[0].native_replacement.annotation.selection.points==row.selection.points
    ij.validate_imagej_roi_import_record(record)
    before=doc.snapshot();doc.commit(ij.prepare_imagej_roi_import(ij.imagej_roi_import_from_record(before,record)))
    assert doc.undo_steps==1 and archive(doc)==record['after_sources']
    assert doc.snapshot().dataset.shares_pixels(before.dataset) and reads.counts().plane_reads==reads.counts().region_reads==0
    doc.undo();assert doc.snapshot().shares_data(before);doc.redo();assert archive(doc)==record['after_sources']

def test_v1_prefix_upgrade_suffix_extraction_and_v2_prefix_append():
    doc,_=target();context=mapping(extent=TARGET)
    first=ij.ImageJRoiImportItem('prior.roi',bare().data,context)
    initial=ij.resolve_imagej_roi_import(doc.snapshot(),[first]);assert initial.record()['version']==1
    doc.commit(ij.prepare_imagej_roi_import(initial));old=archive(doc)
    upgraded=ij.resolve_imagej_roi_import(doc.snapshot(),[item()]);record=upgraded.record()
    assert record['before_sources']==old and record['after_sources']['version']==2
    prefix=record['after_sources']['entries'][0]
    assert {k:v for k,v in prefix.items() if k!='native_replacement'}==old['entries'][0]
    assert prefix['native_replacement'] is None
    request=ij.decode_imagej_roi_import_request(record);assert [v.name for v in request.items]==['original.roi']
    doc.commit(ij.prepare_imagej_roi_import(upgraded))
    appended=ij.resolve_imagej_roi_import(doc.snapshot(),[ij.ImageJRoiImportItem('later.roi',bare().data,context)])
    assert appended.record()['version']==2 and appended.record()['after_sources']['version']==2
    assert appended.record()['after_sources']['entries'][-1]['native_replacement'] is None
    assert [v.name for v in ij.decode_imagej_roi_import_request(appended.record()).items]==['later.roi']
    ij.validate_imagej_roi_import_record(appended.record())


def test_historical_replacements_are_validated_without_retaining_the_whole_prefix():
    doc,reads=target();limits=ij.ImageJRoiImportLimits(max_decoded_bytes=16*1024)
    for i in range(25):
        result=ij.resolve_imagej_roi_import(doc.snapshot(),[item('prior'+str(i)+'.roi')],limits=limits)
        doc.commit(ij.prepare_imagej_roi_import(result))
        doc.commit(ij.prepare_delete_planar_annotation(doc.snapshot(),0))
    assert len(archive(doc)['entries'])==25 and ij.planar_annotations(doc.snapshot())==[]
    record=ij.resolve_imagej_roi_import(doc.snapshot(),[item('suffix.roi')],limits=limits).record()
    ij.validate_imagej_roi_import_record(record)
    assert [v.name for v in ij.decode_imagej_roi_import_request(record).items]==['suffix.roi']
    assert reads.counts().plane_reads==reads.counts().region_reads==0

def test_replace_can_return_to_v1_archive_while_command_retains_v2_before_state():
    doc,_=target();doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[item()])))
    clean=ij.ImageJRoiImportItem('clean.roi',bare().data,mapping(extent=TARGET))
    record=ij.resolve_imagej_roi_import(doc.snapshot(),[clean],ij.ImageJRoiImportMode.REPLACE).record()
    assert record['version']==2 and record['before_sources']['version']==2 and record['after_sources']['version']==1
    assert set(record['after_sources']['entries'][0])=={'name','data','mapping'}
    ij.validate_imagej_roi_import_record(record)
    assert ij.imagej_roi_import_from_record(doc.snapshot(),record).record()==record

@pytest.mark.parametrize('path,value',[
    (('version',),1),(('version',),True),
    (('after_sources','version'),1),
    (('after_sources','entries',0,'native_replacement','extent',0),True),
    (('after_sources','entries',0,'native_replacement','extent',5),3),
    (('after_sources','entries',0,'native_replacement','annotation','coordinate'),0),
    (('after_sources','entries',0,'native_replacement','annotation','name'),'forged'),
    (('after_sources','entries',0,'native_replacement','annotation','selection','points',0,0),1.5),
    (('after_sources','entries',0,'native_replacement','annotation','extra'),1),
    (('after_sources','entries',0,'data'),'AA=='),
    (('after','rows',0,'attributes'),{}),
])
def test_v2_command_tampering_refuses_original_before_live_rebind(path,value):
    doc,_=target();record=ij.resolve_imagej_roi_import(doc.snapshot(),[item()]).record();bad=copy.deepcopy(record)
    node=bad
    for key in path[:-1]:node=node[key]
    node[path[-1]]=value
    for action in [lambda:ij.validate_imagej_roi_import_record(bad),lambda:ij.decode_imagej_roi_import_request(bad),
                   lambda:ij.rebind_imagej_roi_import(doc.snapshot(),bad,[item('explicit.roi')])]:
        with pytest.raises((ValueError,IndexError,OverflowError)):action()
    assert doc.undo_steps==0

@pytest.mark.parametrize('mode',[ij.ImageJRoiImportMode.APPEND,ij.ImageJRoiImportMode.REPLACE])
def test_corrupted_historical_native_replacement_is_admitted_even_for_replace(mode):
    doc,_=target();doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[item()])))
    old=archive(doc);old['entries'][0]['native_replacement']['annotation']['coordinate']=999
    edit=doc.begin_edit();edit.set_extension(SOURCES,old);doc.commit(edit);before=doc.snapshot()
    with pytest.raises((ValueError,IndexError)):
        ij.resolve_imagej_roi_import(before,[ij.ImageJRoiImportItem('new.roi',bare().data,mapping(extent=TARGET))],mode)
    assert doc.snapshot().shares_data(before)

def test_original_envelope_is_admitted_before_bad_replacement_and_bare_mode_refuses():
    doc,_=target();bad_row=ij.PlanarAnnotation('bad',ij.Selection('point',[(1,1)]),'xy',999)
    choice=ij.ImageJRoiNativeReplacement(TARGET,bad_row)
    wire=bytearray(envelope().data);wire[0]=0
    with pytest.raises(ValueError,match='signature'):
        ij.resolve_imagej_roi_import(doc.snapshot(),[ij.ImageJRoiImportItem('bad.roi',bytes(wire),native_replacement=choice)])
    with pytest.raises(ValueError):
        ij.resolve_imagej_roi_import(doc.snapshot(),[ij.ImageJRoiImportItem('bare.roi',bare().data,mapping(extent=TARGET),replacement())])
    with pytest.raises(ValueError):
        ij.resolve_imagej_roi_import(doc.snapshot(),[ij.ImageJRoiImportItem('missing.roi',bare().data,native_replacement=replacement())])

def test_all_six_dimensions_and_all_or_nothing_late_member_failure():
    doc,_=target();before=doc.snapshot()
    for extent in [[4,*TARGET[1:]],[*TARGET[:-1],3]]:
        with pytest.raises(ValueError,match='explicit rebinding'):
            ij.resolve_imagej_roi_import(before,[item(choice=replacement(extent=extent))])
    with pytest.raises(ValueError):
        ij.resolve_imagej_roi_import(before,[item(),ij.ImageJRoiImportItem('bad-last.roi',b'bad',native_replacement=replacement())])
    assert doc.snapshot().shares_data(before) and doc.undo_steps==0 and archive(doc) is None

@pytest.mark.parametrize('extent',[[True,2,8,10,14,1],[0,2,8,10,14,1],[3,2,8,10,14],[-1,2,8,10,14,1]])
def test_python_extent_type_and_positive_boundaries(extent):
    with pytest.raises((ValueError,TypeError,OverflowError)):
        ij.ImageJRoiNativeReplacement(extent,replacement().annotation)

def test_borrowed_replacement_budget_and_provenance_budget_refuse_without_reads():
    doc,reads=target()
    for limits in [ij.ImageJRoiImportLimits(max_decoded_bytes=1),ij.ImageJRoiImportLimits(max_provenance_bytes=128)]:
        with pytest.raises(ValueError):ij.resolve_imagej_roi_import(doc.snapshot(),[item()],limits=limits)
    assert doc.undo_steps==0 and reads.counts().plane_reads==0

def test_new_owner_rebind_session_and_noop_shared_controller(tmp_path):
    doc,reads=target();original=ij.resolve_imagej_roi_import(doc.snapshot(),[item()],ij.ImageJRoiImportMode.REPLACE).record()
    other,other_reads=target();rebound=ij.rebind_imagej_roi_import(other.snapshot(),original,[item()],ij.ImageJRoiImportMode.REPLACE)
    assert rebound.record()['source']['document_id']!=original['source']['document_id']
    editor=_desktop.Editor(str(tmp_path/'preferences.json'));editor.add_native_document(other);editor.recording=True
    editor.action('replay_imagej_roi_import',json.dumps({'record':rebound.record()}))
    before=other.snapshot();history=other.undo_steps;workflow=json.loads(editor.workflow)
    repeated=ij.resolve_imagej_roi_import(before,[item()],ij.ImageJRoiImportMode.REPLACE)
    editor.action('replay_imagej_roi_import',json.dumps({'record':repeated.record()}))
    assert other.snapshot().shares_data(before) and other.undo_steps==history and json.loads(editor.workflow)==workflow
    assert reads.counts().plane_reads==other_reads.counts().plane_reads==0
    restored=ij.decode_session(ij.encode_session(ij.SessionCapture([other],workflow=workflow)))
    assert archive(restored.documents[0])==archive(other) and restored.workflow==workflow
    ij.validate_imagej_roi_import_record(restored.workflow[0])

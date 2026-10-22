#include "histogram_workflow_ui_probe.hpp"
#include "histogram_async_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/native_workflow_program.hpp>
#include <imagejxx/session.hpp>
#include <condition_variable>
#include <fstream>
#include <thread>
namespace imagejxx::desktop {
namespace {
void require(bool ok,const std::string &message){if(!ok)throw std::runtime_error(message);}
struct Gate {
  std::mutex mutex;std::condition_variable changed;bool entered=false,open=false;
  void block(){std::unique_lock<std::mutex> lock(mutex);entered=true;changed.notify_all();require(changed.wait_for(lock,std::chrono::seconds(30),[&]{return open;}),"Workflow gate timed out");}
  void wait(){std::unique_lock<std::mutex> lock(mutex);require(changed.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}),"Workflow gate did not start");}
  void release(){std::lock_guard<std::mutex> lock(mutex);open=true;changed.notify_all();}
};
std::string read(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
}
Json histogramWorkflowUiCase(Window &w,const std::string &id,const std::string &host,const std::string &directory){
  namespace fs=std::filesystem;const auto root=fs::absolute(fs::u8path(directory));
  require(!directory.empty()&&!fs::exists(root)&&w.documents().empty(),"Fresh histogram workflow fixture required");
  require(id=="run"||id=="captured"||id=="readonly"||id=="disabled"||id=="preview"||id=="conflict"||id=="stale"||id=="cancel"||id=="batch"||id=="choices"||id=="raw_choices"||id=="restore"||id=="invalid_choices"||id=="authored"||id=="batch_statistics"||id=="batch_statistics_review"||id=="batch_statistics_conflict"||id=="batch_statistics_cancel"||id=="batch_statistics_setup","Unknown histogram workflow case");
  fs::create_directories(root);Stack planes;
  for(int z=0;z<3;++z){Image p(2,2,1,PixelType::UInt16);for(int i=0;i<4;++i)p.pixels[i]=4*z+i;planes.push_back(std::move(p));}
  DatasetReadObserver reads;auto owner=std::make_shared<imagejxx::Document>(Dataset::from_planes({1,1,3,2,2,1},std::move(planes)).observe_reads(reads));
  w.addNativeDocument(owner);w.recording=true;
  TargetRequest target;target.scope=TargetScope::AllZ;target.roi=RoiPolicy::WholePlane;
  TargetHistogramOptions options;options.binning=HistogramBinning::UniformRange;options.bins=4;options.range=std::array<double,2>{0,20};
  const auto record=resolve_histogram_command(owner->snapshot(),options,target,true).record();
  NativeWorkflowHistogram first;first.name="before.csv";first.target=target;first.original=record;
  auto last_step=first;last_step.name="after.csv";
  NativeWorkflowOperation add;add.operation="add";add.parameters={{"value",5.}};add.target=target;
  const bool statistics_case=id.rfind("batch_statistics",0)==0;
  const bool batch_case=id=="batch"||statistics_case;
  const bool choice_case=id=="choices"||id=="raw_choices"||id=="restore"||id=="invalid_choices"||id=="authored";
  if(id=="authored") {
    first.original.reset();NativeWorkflowHistogramOptions value;value.histogram=options;
    value.histogram.bins=6;value.histogram.range=std::array<double,2>{0,12};value.log_counts=false;first.authored_options=value;
  }
  if(statistics_case) {
    NativeWorkflowHistogramOptions value;value.histogram=options;value.histogram.bins=1;value.histogram.range=std::array<double,2>{0,10};
    value.log_counts=false;first.authored_options=value;last_step.authored_options=value;
  }
  const bool single=id=="captured"||id=="readonly";
  w.workflow=id=="captured"?Json::array({metadataJson(record)}):Json::array({metadataJson(make_native_workflow_program(single?std::vector<NativeWorkflowStep>{first}:std::vector<NativeWorkflowStep>{first,add,last_step}).record())});
  const auto original_workflow=w.workflow;auto expected_workflow=w.workflow;const auto source=owner->snapshot();
  Json report={{"contract",statistics_case?"p10s.batch-histogram-statistics.ui.001":choice_case?"p10r.histogram-workflow-options.ui.001":"p10q.histogram-workflow.ui.001"},{"context",host},{"case_id",id},{"acceptance_claim",false},{"state","running"},{"frames",Json::array()},{"controls",Json::array()}};
  report["original_workflow"]=original_workflow;report["defaults_before"]=w.preferencesJson();
  auto gate=std::make_shared<Gate>();struct Cleanup{std::shared_ptr<Gate> gate;~Cleanup(){gate->release();}} cleanup{gate};
  struct Failure{Json &report;fs::path root;~Failure(){if(report.at("state")!="observed")try{writeJson((root/"failure.json").u8string(),report);}catch(...){}}} failure{report,root};
  const auto admission_before=reads.counts();w.action(batch_case?"native_batch":"run_workflow");const auto admission_after=reads.counts();
  report["admission_reads"]={{"before_plane",admission_before.plane_reads},{"after_plane",admission_after.plane_reads},{"before_region",admission_before.region_reads},{"after_region",admission_after.region_reads}};
  require(admission_before.plane_reads==admission_after.plane_reads && admission_before.region_reads==admission_after.region_reads,"Opening workflow admission read samples");
  HeadlessInputSession session(w);Json last;
  auto frame=[&](const Json &events=Json::array()){
    require(report["frames"].size()<2400,"Histogram workflow frame bound");last=session.frame(events);
    require(last.at("input_frame").at("raw_inputs")==events,"Workflow raw input mismatch");
    report["frames"].push_back({{"events",events},{"input_frame",last.at("input_frame")},{"panel",last.at("native_workflow")}});
  };
  auto panel=[&]{return last.at("native_workflow");};
  auto click=[&](const Json &hit,const std::string &name){
    require(hit.at("enabled").get<bool>()&&hit.at("id").get<std::uint64_t>(),"Unavailable histogram workflow control: "+name);
    report["controls"].push_back({{"name",name},{"control",hit}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",hit.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();
  };
  auto control=[&](const std::string &key){
    for(int retry=0;retry<55;++retry){const auto p=panel();require(p.at("controls").contains(key),"Missing workflow control: "+key);
      auto hit=p.at("controls").at(key);if(hit.at("visible").get<bool>())return hit;
      const auto bounds=p.at("window");frame(Json::array({{{"type","position"},{"space","screen"},{"point",{bounds[2].get<double>()-14,(bounds[1].get<double>()+bounds[3].get<double>())/2}}},{{"type","wheel"},{"x",0},{"y",hit.at("point")[1].get<double>()<bounds[1].get<double>()?3:-3}}}));frame();
    }throw std::runtime_error("Could not reveal workflow control: "+key);
  };
  auto press=[&](const std::string &key){click(control(key),key);};
  auto type_hit=[&](const Json &hit,const std::string &key,const std::string &value){
    click(hit,key);for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  auto text=[&](const std::string &key,const std::string &value){type_hit(control(key),key,value);};
  auto choose=[&](const std::string &key,int index){press(key);press(key+"."+std::to_string(index));};
  auto settle=[&]{const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(20);while(panel().at("working").get<bool>()){require(std::chrono::steady_clock::now()<end,"Workflow worker timed out");std::this_thread::sleep_for(std::chrono::milliseconds(3));frame();}frame();};
  frame();frame();press("cancel");frame();
  if(batch_case){
    Image other(2,2,1,PixelType::UInt16);for(auto &v:other.pixels)v=17;
    imagejxx::Document second(Dataset::from_planes({1,1,1,2,2,1},{other}));
    SessionCapture capture;capture.documents={source,second.snapshot()};capture.results={metadata_object({{"kept",42}})};
    save_session((root/"input.ijx").u8string(),capture);fs::create_directory(root/"batch-output");
    click(last.at("host_controls").at("workflow.session_batch"),"workflow.session_batch");
    text("batch.input",(root/"input.ijx").u8string());press("batch.add");
    text("batch.directory",(root/"batch-output").u8string());press("batch.all_documents");
    if(statistics_case&&id!="batch_statistics_review")press("batch.histogram_statistics");
    press("batch.confirm");press("file_consent");
    if(id=="batch_statistics_setup") {
      press("batch.setup_save");type_hit(last.at("file_browser").at("controls").at("path"),"browser.path.save_setup",(root/"setup.json").u8string());
      click(last.at("file_browser").at("controls").at("submit"),"browser.submit.save_setup");settle();
      require(panel().at("error")==""&&fs::exists(root/"setup.json"),"Statistics setup save failed");
      report["setup_record"]=Json::parse(read(root/"setup.json"));press("batch.histogram_statistics");
      press("batch.setup_load");type_hit(last.at("file_browser").at("controls").at("path"),"browser.path.load_setup",(root/"setup.json").u8string());
      click(last.at("file_browser").at("controls").at("submit"),"browser.submit.load_setup");settle();
      report["loaded_setup"]=panel();require(panel().at("histogram_statistics").get<bool>()&&!panel().at("file_consent").get<bool>(),"Statistics setup lost intent or restored consent");
      press("batch.confirm");press("file_consent");
    }
  }else click(last.at("host_controls").at("workflow.run"),"workflow.run");
  require(panel().at("visible").get<bool>(),"Histogram workflow did not open");
  report["opened"]=panel();
  for(int i=0;i<(single?1:3);++i){
    press("step."+std::to_string(i));
    if(i==0&&id=="disabled")press("enabled");
    if(i==0&&id=="batch")text("output_token","initial.csv");
    if(i==0&&id=="run"){choose("scope",2);text("stop.2","2");}
    if(i==0&&choice_case) {
      if(id!="authored")press("histogram.author");
      if(id=="raw_choices")press("histogram.custom");
      else {text("histogram.bins",id=="invalid_choices"?"0":id=="authored"?"3":"5");text("histogram.lower","0");text("histogram.upper",id=="authored"?"12":"10");}
      if(id!="authored")press("histogram.log");
      if(id=="restore")press("histogram.author");
    }
    if(i==0&&id=="run") {
      press("browse");type_hit(last.at("file_browser").at("controls").at("path"),"browser.path",(root/"before.csv").u8string());
      click(last.at("file_browser").at("controls").at("submit"),"browser.submit");
    } else if(!choice_case&&i!=1&&!batch_case&&id!="preview"&&id!="readonly"&&id!="captured"&&id!="disabled")text("path",(root/(i==0?"before.csv":"after.csv")).u8string());
    if(!choice_case&&i==0&&!batch_case&&id!="preview"&&id!="readonly"&&id!="captured"&&id!="disabled")press("file_consent");
    press("confirm");
  }
  if(choice_case && id!="invalid_choices") {
    press("prepare_program");settle();require(panel().at("program_ready").get<bool>() && panel().at("error")=="","Edited choices did not prepare");
    require(w.workflow==original_workflow && w.results.empty(),"Program preparation published effects");
    press("keep_program");expected_workflow=w.workflow;report["retained_program"]=w.workflow;
    frame();click(last.at("host_controls").at("workflow.run"),"workflow.run.retained");
    for(int i=0;i<3;++i){press("step."+std::to_string(i));press("confirm");}
  }
  auto scheduler=HistogramAsyncUiProbeAccess::scheduler(w);Job held;
  if(id=="cancel"||id=="batch_statistics_cancel"){scheduler->set_limits({1,1024ULL*1024*1024,64});held=scheduler->submit([gate](const JobContext &){gate->block();});gate->wait();}
  press(id=="preview"?"prepare_preview":"prepare");
  if(id=="invalid_choices") {
    settle();report["rejected"]=panel();
    require(panel().at("error")!=""&&!panel().at("ready").get<bool>()&&w.results.empty()&&owner->snapshot().shares_data(source),"Invalid histogram choices published effects");press("cancel");
  }else if(id=="cancel"||id=="batch_statistics_cancel"){
    report["queued"]=panel();require(panel().at("working").get<bool>(),"Workflow did not queue");press("cancel");gate->release();held.wait();w.wait();frame();
    require(!panel().at("visible").get<bool>()&&w.results.empty()&&owner->undo_steps()==0,"Canceled workflow published effects");
  }else{
    settle();require(panel().at("error")==""&&panel().at("ready").get<bool>(),"Histogram workflow did not prepare: "+panel().dump());report["prepared"]=panel();
    require(w.results.empty()&&owner->snapshot().shares_data(source),"Preparation published live effects");
    if(id=="stale"){
      auto tx=owner->begin_edit();tx.set_property("changed",true);owner->commit(tx);frame();
      require(!panel().at("visible").get<bool>()&&w.results.empty(),"Stale workflow remained publishable");
    }else if(id=="preview"){
      require(panel().at("preview_only").get<bool>()&&!control("apply").at("enabled").get<bool>(),"Preview became publishable");press("cancel");
    }else{
      if(id=="conflict"){std::ofstream file(root/"after.csv",std::ios::binary);file<<"foreign winner";}
      if(id=="batch_statistics_conflict"){std::ofstream file(root/"batch-output/input-processed.histograms.csv",std::ios::binary);file<<"foreign winner";}
      press("apply");
      if(id=="readonly")press("histogram.0.bins");
      report["applied"]=panel();
      if(id=="batch_statistics_conflict") {
        require(panel().at("error")!=""&&owner->snapshot().shares_data(source)&&w.results.empty(),"Statistics conflict changed owner/results");
        require(read(root/"batch-output/input-processed.histograms.csv")=="foreign winner"&&!fs::exists(root/"batch-output/input-processed.ijx"),"Statistics publication race overwrote a file");
      }else if(id=="conflict"){
        require(panel().at("error")!=""&&owner->snapshot().shares_data(source)&&w.results.empty(),"CSV conflict changed image/results");
        require(fs::exists(root/"before.csv")&&read(root/"after.csv")=="foreign winner","Partial file publication policy violated");
      }else{
        require(panel().at("error")=="","Workflow publication failed: "+panel().dump());
        require(batch_case?w.results.empty():w.results.size()==std::size_t(single||id=="disabled"?1:2),"Histogram summaries missing");
        require(owner->undo_steps()==(batch_case||single?0:1),"Histogram workflow history mismatch");
      }
      press("cancel");
    }
  }
  report["results"]=w.results;report["workflow"]=w.workflow;report["undo_steps"]=owner->undo_steps();
  report["final_values"]=owner->snapshot().data().dataset.plane({0,0,0})->pixels;
  report["csv"]=Json::object();for(const auto *name:{"before.csv","after.csv"})if(fs::exists(root/name))report["csv"][name]=read(root/name);
  if(batch_case){
    report["batch_files"]=Json::object();for(const auto &entry:fs::directory_iterator(root/"batch-output"))report["batch_files"][entry.path().filename().u8string()]=entry.path().extension()==".csv"?Json(read(entry.path())):Json(entry.file_size());
  }
  report["defaults_after"]=w.preferencesJson();
  require(w.workflow==expected_workflow,"Running a recorded workflow duplicated its recorder");
  report["state"]="observed";writeJson((root/"case.json").u8string(),report);return report;
}
}

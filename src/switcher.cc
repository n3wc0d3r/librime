// vim: set sts=2 sw=2 et:
// encoding: utf-8
//
// Copyleft 2011 RIME Developers
// License: GPLv3
//
// 2011-12-07 GONG Chen <chen.sst@gmail.com>
//
#include <string>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <rime/candidate.h>
#include <rime/common.h>
#include <rime/composition.h>
#include <rime/context.h>
#include <rime/key_event.h>
#include <rime/menu.h>
#include <rime/processor.h>
#include <rime/schema.h>
#include <rime/switcher.h>
#include <rime/translation.h>

static const char* kRightArrow = " \xe2\x86\x92 ";

namespace rime {

class SwitcherOption : public Candidate {
 public:
  SwitcherOption(Schema *schema)
      : Candidate("schema", 0, 0),
        text_(schema->schema_name()),
        comment_(),
        value_(schema->schema_id()),
        auto_save_(true) {}
  SwitcherOption(const std::string &current_state_label,
                 const std::string &next_state_label,
                 const std::string &option_name,
                 bool current_state,
                 bool auto_save)
      : Candidate(current_state ? "switch_off" : "switch_on", 0, 0),
        text_(current_state_label + kRightArrow + next_state_label),
        value_(option_name),
        auto_save_(auto_save) {}

  void Apply(Engine *target_engine, Config *user_config);
  
  const std::string& text() const { return text_; }
  const std::string comment() const { return comment_; }
  
 protected:
  std::string text_;
  std::string comment_;
  std::string value_;
  bool auto_save_;
};

void SwitcherOption::Apply(Engine *target_engine, Config *user_config) {
  if (type() == "schema") {
    const std::string &current_schema_id(target_engine->schema()->schema_id());
    if (value_ != current_schema_id) {
      target_engine->set_schema(new Schema(value_));
    }
    if (auto_save_ && user_config) {
      user_config->SetString("var/previously_selected_schema", value_);
    }
    return;
  }
  if (type() == "switch_off" || type() == "switch_on") {
    bool option_is_on = (type() == "switch_on");
    target_engine->context()->set_option(value_, option_is_on);
    if (auto_save_ && user_config) {
      user_config->SetBool("var/option/" + value_, option_is_on);
    }
    return;
  }
}

Switcher::Switcher() : Engine(new Schema),
                       target_engine_(NULL),
                       active_(false) {
  context_->set_option("dumb", true);  // not going to commit anything
  
  // receive context notifications
  context_->select_notifier().connect(
      boost::bind(&Switcher::OnSelect, this, _1));

  user_config_.reset(Config::Require("config")->Create("user"));
  InitializeSubProcessors();
  LoadSettings();
}

Switcher::~Switcher() {
}

void Switcher::Attach(Engine *engine) {
  target_engine_ = engine;
  // restore saved options
  if (user_config_) {
    BOOST_FOREACH(const std::string& option_name, save_options_) {
      bool value = false;
      if (user_config_->GetBool("var/option/" + option_name, &value)) {
        engine->context()->set_option(option_name, value);
      }
    }
  }
}

bool Switcher::ProcessKeyEvent(const KeyEvent &key_event) {
  BOOST_FOREACH(const KeyEvent &hotkey, hotkeys_) {
    if (key_event == hotkey) {
      if (!active_ && target_engine_) {
        Activate();
      }
      else if (active_) {
        HighlightNextSchema();
      }
      return true;
    }
  }
  if (active_) {
    BOOST_FOREACH(shared_ptr<Processor> &p, processors_) {
      if (Processor::kNoop != p->ProcessKeyEvent(key_event))
        return true;
    }
    if (key_event.release() || key_event.ctrl() || key_event.alt())
      return true;
    int ch = key_event.keycode();
    if (ch == XK_space || ch == XK_Return) {
      context_->ConfirmCurrentSelection();
    }
    else if (ch == XK_Escape) {
      Deactivate();
    }
    return true;
  }
  return false;
}

void Switcher::HighlightNextSchema() {
  Composition *comp = context_->composition();
  if (!comp || comp->empty() || !comp->back().menu)
    return;
  Segment& seg(comp->back());
  int index = seg.selected_index;
  shared_ptr<SwitcherOption> option;
  do {
    ++index;  // next
    int candidate_count = seg.menu->Prepare(index + 1);
    if (candidate_count <= index) {
      index = 0;  // passed the end; rewind
      break;
    }
    else {
      option = As<SwitcherOption>(seg.GetCandidateAt(index));
    }
  }
  while (!option || option->type() != "schema");
  seg.selected_index = index;
  seg.tags.insert("paging");
  return;
}

Schema* Switcher::CreateSchema() {
  Config *config = schema_->config();
  if (!config) return NULL;
  ConfigListPtr schema_list = config->GetList("schema_list");
  if (!schema_list) return NULL;
  std::string previous;
  if (user_config_) {
    user_config_->GetString("var/previously_selected_schema", &previous);
  }
  std::string recent;
  for (size_t i = 0; i < schema_list->size(); ++i) {
    ConfigMapPtr item = As<ConfigMap>(schema_list->GetAt(i));
    if (!item) continue;
    ConfigValuePtr schema_property = item->GetValue("schema");
    if (!schema_property) continue;
    const std::string &schema_id(schema_property->str());
    if (previous.empty() || previous == schema_id) {
      recent = schema_id;
      break;
    }
    if (recent.empty())
      recent = schema_id;
  }
  if (recent.empty())
    return NULL;
  else
    return new Schema(recent);
}

void Switcher::OnSelect(Context *ctx) {
  LOG(INFO) << "a switcher option is selected.";
  Segment &seg(ctx->composition()->back());
  shared_ptr<SwitcherOption> option =
      As<SwitcherOption>(seg.GetSelectedCandidate());
  if (!option) return;
  if (target_engine_) {
    option->Apply(target_engine_, user_config_.get());
  }
  Deactivate();
}

void Switcher::Activate() {
  LOG(INFO) << "switcher is activated.";
  Config *config = schema_->config();
  if (!config) return;
  ConfigListPtr schema_list = config->GetList("schema_list");
  if (!schema_list) return;

  shared_ptr<FifoTranslation> switcher_options = make_shared<FifoTranslation>();
  Schema *current_schema = NULL;
  // current schema comes first
  if (target_engine_ && target_engine_->schema()) {
    current_schema = target_engine_->schema();
    switcher_options->Append(make_shared<SwitcherOption>(current_schema));
    // add custom switches
    Config *custom = current_schema->config();
    if (custom) {
      ConfigListPtr switches = custom->GetList("switches");
      if (switches) {
        Context *context = target_engine_->context();
        for (size_t i = 0; i < switches->size(); ++i) {
          ConfigMapPtr item = As<ConfigMap>(switches->GetAt(i));
          if (!item) continue;
          ConfigValuePtr name_property = item->GetValue("name");
          if (!name_property) continue;
          ConfigListPtr states = As<ConfigList>(item->Get("states"));
          if (!states || states->size() != 2) continue;
          bool current_state = context->get_option(name_property->str());
          bool auto_save = (save_options_.find(name_property->str()) != save_options_.end());
          switcher_options->Append(
              boost::make_shared<SwitcherOption>(
                  states->GetValueAt(current_state)->str(),
                  states->GetValueAt(1 - current_state)->str(),
                  name_property->str(),
                  current_state,
                  auto_save));
        }
      }
    }
  }
  // load schema list
  for (size_t i = 0; i < schema_list->size(); ++i) {
    ConfigMapPtr item = As<ConfigMap>(schema_list->GetAt(i));
    if (!item) continue;
    ConfigValuePtr schema_property = item->GetValue("schema");
    if (!schema_property) continue;
    const std::string &schema_id(schema_property->str());
    if (current_schema && schema_id == current_schema->schema_id())
      continue;
    scoped_ptr<Schema> schema(new Schema(schema_id));
    // the switcher option doesn't own the schema object
    switcher_options->Append(make_shared<SwitcherOption>(schema.get()));
  }
  // assign menu to switcher's context
  Composition *comp = context_->composition();
  if (comp->empty()) {
    context_->set_input(" ");
    Segment seg(0, 0);
    seg.prompt = caption_;
    comp->AddSegment(seg);
  }
  shared_ptr<Menu> menu = make_shared<Menu>();
  comp->back().menu = menu;
  menu->AddTranslation(switcher_options);
  // activated!
  active_ = true;
}

void Switcher::Deactivate() {
  context_->Clear();
  active_ = false;
}

void Switcher::LoadSettings() {
  Config *config = schema_->config();
  if (!config) return;
  if (!config->GetString("switcher/caption", &caption_) || caption_.empty()) {
    caption_ = ":-)";
  }
  ConfigListPtr hotkeys = config->GetList("switcher/hotkeys");
  if (!hotkeys) return;
  hotkeys_.clear();
  for (size_t i = 0; i < hotkeys->size(); ++i) {
    ConfigValuePtr value = hotkeys->GetValueAt(i);
    if (!value) continue;
    hotkeys_.push_back(KeyEvent(value->str()));
  }
  ConfigListPtr options = config->GetList("switcher/save_options");
  if (!options) return;
  save_options_.clear();
  for (ConfigList::Iterator it = options->begin(); it != options->end(); ++it) {
    ConfigValuePtr option_name = As<ConfigValue>(*it);
    if (!option_name) continue;
    save_options_.insert(option_name->str());
  }
}

void Switcher::InitializeSubProcessors() {
  processors_.clear();
  {
    Processor::Component *c = Processor::Require("key_binder");
    if (!c) {
      LOG(WARNING) << "key_binder not available.";
    }
    else {
      shared_ptr<Processor> p(c->Create(this));
      processors_.push_back(p);
    }
  }
  {
    Processor::Component *c = Processor::Require("selector");
    if (!c) {
      LOG(WARNING) << "selector not available.";
    }
    else {
      shared_ptr<Processor> p(c->Create(this));
      processors_.push_back(p);
    }
  }
}

}  // namespace rime

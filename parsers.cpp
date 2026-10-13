#include "Parsers.h"
#include <cctype>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <tinyxml2.h>
#include <toml++/toml.h>
#include <vector>
#include <yaml-cpp/yaml.h>
using namespace std;

namespace {
// Lowercase a copy (for tolerant boolean parsing).
static std::string lower_copy(std::string s) {
  for (char &ch : s)
    ch = static_cast<char>(std::tolower((unsigned char)ch));
  return s;
}

// First non-whitespace byte: '<' is XML, '{' is JSON. Used to order the
// auto-detect attempts so JSON documents (which are also valid YAML) are
// not misread by the YAML parser first.
static char sniff_format(const std::string_view sv) {
  for (char c : sv)
    if (!isspace((unsigned char)c))
      return c;
  return 0;
}

// ---- tolerant JSON accessors (numbers/bools may arrive as strings) ----
static float json_float(const nlohmann::json &j, const char *key, float def) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null())
    return def;
  if (it->is_number())
    return it->get<float>();
  if (it->is_boolean())
    return it->get<bool>() ? 1.0f : 0.0f;
  if (it->is_string()) {
    try {
      size_t pos = 0;
      float r = std::stof(it->get<std::string>(), &pos);
      if (pos == it->get<std::string>().size())
        return r;
    } catch (...) {
    }
  }
  return def;
}

static int json_int(const nlohmann::json &j, const char *key, int def) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null())
    return def;
  if (it->is_number_integer())
    return static_cast<int>(it->get<long long>());
  if (it->is_number())
    return static_cast<int>(it->get<double>());
  if (it->is_boolean())
    return it->get<bool>() ? 1 : 0;
  if (it->is_string()) {
    try {
      size_t pos = 0;
      long long r = std::stoll(it->get<std::string>(), &pos);
      if (pos == it->get<std::string>().size())
        return static_cast<int>(r);
    } catch (...) {
    }
  }
  return def;
}

static bool json_bool(const nlohmann::json &j, const char *key, bool def) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null())
    return def;
  if (it->is_boolean())
    return it->get<bool>();
  if (it->is_number())
    return it->get<double>() != 0.0;
  if (it->is_string()) {
    std::string s = lower_copy(it->get<std::string>());
    if (s == "true" || s == "1")
      return true;
    if (s == "false" || s == "0")
      return false;
  }
  return def;
}

static std::string json_str(const nlohmann::json &j, const char *key) {
  auto it = j.find(key);
  if (it != j.end() && it->is_string())
    return it->get<std::string>();
  return "";
}

// ---- tolerant TOML accessors (ints may arrive as floats/strings) ----
static std::optional<int64_t> toml_opt_i64(const toml::node &n) {
  if (auto v = n.value<int64_t>())
    return *v;
  if (auto v = n.value<double>())
    return static_cast<int64_t>(*v);
  if (auto v = n.value<bool>())
    return *v ? 1 : 0;
  if (auto v = n.value<std::string>()) {
    try {
      size_t pos = 0;
      long long r = std::stoll(*v, &pos);
      if (pos == v->size())
        return r;
    } catch (...) {
    }
  }
  return std::nullopt;
}

static std::optional<bool> toml_opt_bool(const toml::node &n) {
  if (auto v = n.value<bool>())
    return *v;
  if (auto v = n.value<int64_t>())
    return *v != 0;
  if (auto v = n.value<double>())
    return *v != 0.0;
  if (auto v = n.value<std::string>()) {
    std::string s = lower_copy(*v);
    if (s == "true" || s == "1")
      return true;
    if (s == "false" || s == "0")
      return false;
  }
  return std::nullopt;
}

static const toml::node &toml_req(const toml::table &t, const char *key) {
  const toml::node *n = t.get(key);
  if (!n)
    throw std::runtime_error(std::string("Missing key: ") + key);
  return *n;
}

static std::string toml_req_str(const toml::table &t, const char *key) {
  const toml::node &n = toml_req(t, key);
  if (auto v = n.value<std::string>())
    return *v;
  throw std::runtime_error(std::string("Bad string value for key: ") + key);
}

static int64_t toml_req_i64(const toml::table &t, const char *key) {
  const toml::node &n = toml_req(t, key);
  if (auto v = toml_opt_i64(n))
    return *v;
  throw std::runtime_error(std::string("Bad numeric value for key: ") + key);
}

static bool toml_req_bool(const toml::table &t, const char *key) {
  const toml::node &n = toml_req(t, key);
  if (auto v = toml_opt_bool(n))
    return *v;
  throw std::runtime_error(std::string("Bad boolean value for key: ") + key);
}
} // namespace
template <>
void ParseTestSettings<ParseType::YAML>(const std::string_view &sv,
                                        Testcases &tc) {
  YAML::Node node = YAML::Load(std::string(sv));
  YAML::Node info = node["ExamInformation"];
  if (!info)
    throw std::runtime_error("Missing ExamInformation");
  YAML::Node subtestcases = info["TestCase"];

  // Tolerant accessors: Themis YAML quotes scalars ('false', '1'), which
  // strict as<bool>/as<int> reject. Missing required keys throw so the
  // auto-detect fallback keeps working.
  auto ys = [](const YAML::Node &n, const char *k) -> std::string {
    const YAML::Node v = n[k];
    if (!v)
      throw std::runtime_error(std::string("Missing key: ") + k);
    try {
      return v.as<std::string>();
    } catch (...) {
      throw std::runtime_error(std::string("Bad string value for key: ") + k);
    }
  };
  auto yb = [](const YAML::Node &n, const char *k) -> bool {
    const YAML::Node v = n[k];
    if (!v)
      throw std::runtime_error(std::string("Missing key: ") + k);
    try {
      return v.as<bool>();
    } catch (...) {
    }
    try {
      std::string s = lower_copy(v.as<std::string>());
      if (s == "true" || s == "1")
        return true;
      if (s == "false" || s == "0")
        return false;
    } catch (...) {
    }
    throw std::runtime_error(std::string("Bad boolean value for key: ") + k);
  };
  auto yf = [](const YAML::Node &n, const char *k, float def,
               bool required) -> float {
    const YAML::Node v = n[k];
    if (!v) {
      if (required)
        throw std::runtime_error(std::string("Missing key: ") + k);
      return def;
    }
    try {
      return v.as<float>();
    } catch (...) {
    }
    try {
      return std::stof(v.as<std::string>());
    } catch (...) {
    }
    if (!required)
      return def;
    throw std::runtime_error(std::string("Bad numeric value for key: ") + k);
  };
  auto yi = [](const YAML::Node &n, const char *k, int def,
               bool required) -> int {
    const YAML::Node v = n[k];
    if (!v) {
      if (required)
        throw std::runtime_error(std::string("Missing key: ") + k);
      return def;
    }
    try {
      return v.as<int>();
    } catch (...) {
    }
    try {
      return std::stoi(v.as<std::string>());
    } catch (...) {
    }
    if (!required)
      return def;
    throw std::runtime_error(std::string("Bad numeric value for key: ") + k);
  };
  /*
e.g.
```yaml
ExamInformation:
  TestCase:
    - Name: 00265ae7f658443a9fdd4e6b74562bf6
      Mark: '-1'
      TimeLimit: '-1'
      MemoryLimit: '-1'
  Name: CBAI3
  InputFile: CBAI3.INP
  UseStdIn: 'false'
  OutputFile: CBAI3.OUT
  UseStdOut: 'false'
  EvaluatorName: C1LinesWordsIgnoreCase.dll
  Mark: '1' # or its corresponding int type
  TimeLimit: '1'
  MemoryLimit: '1024'
```
  */
  tc.Name = ys(info, "Name");
  tc.InputFile = ys(info, "InputFile");
  tc.OutputFile = ys(info, "OutputFile");
  tc.UseStdIn = yb(info, "UseStdIn");
  tc.UseStdOut = yb(info, "UseStdOut");
  tc.Mark = yf(info, "Mark", 1.0f, true);
  tc.MemoryLimit = yi(info, "MemoryLimit", 1024, true);
  tc.TimeLimit = yf(info, "TimeLimit", 1.0f, true);
  tc.EvaluatorName = ys(info, "EvaluatorName");
  auto one_subtest = [&](const YAML::Node &test) {
    Subtest _test;
    _test.Name = ys(test, "Name");
    _test.Mark = yf(test, "Mark", -1.0f, false);
    _test.TimeLimit = yf(test, "TimeLimit", -1.0f, false);
    _test.MemoryLimit = yi(test, "MemoryLimit", -1, false);
    tc.subtests.push_back(std::move(_test));
  };
  if (subtestcases) {
    if (subtestcases.IsSequence()) {
      for (YAML::Node test : subtestcases)
        one_subtest(test);
    } else if (subtestcases.IsMap()) {
      one_subtest(subtestcases); // single-object form, like the JSON dialect
    }
  }
}
namespace {
// impl detail for XML low-level parsing

static bool attr_bool(const tinyxml2::XMLElement *e, const char *name,
                      bool def = false) {
  if (const char *v = e->Attribute(name)) {
    std::string s = lower_copy(v);
    return s == "true" || s == "1";
  }
  return def;
}

static int attr_int(const tinyxml2::XMLElement *e, const char *name,
                    int def = 0) {
  e->QueryIntAttribute(name, &def);
  return def;
}

static float attr_float(const tinyxml2::XMLElement *e, const char *name,
                        float def = 0.f) {
  e->QueryFloatAttribute(name, &def);
  return def;
}

static std::string attr_str(const tinyxml2::XMLElement *e, const char *name) {
  const char *v = e->Attribute(name);
  return v ? v : "";
}
} // namespace
template <>
void ParseTestSettings<ParseType::XML>(const std::string_view &sv,
                                       Testcases &tc) {
  /*
e.g.
```xml
<ExamInformation Name="CBAI3" InputFile="CBAI3.INP" UseStdIn="false"
OutputFile="CBAI3.OUT" UseStdOut="false"
EvaluatorName="C1LinesWordsIgnoreCase.dll" Mark="1" TimeLimit="1"
MemoryLimit="1024"> <TestCase Name="00265ae7f658443a9fdd4e6b74562bf6" Mark="-1"
TimeLimit="-1" MemoryLimit="-1"/>
</ExamInformation>
```*/
  tinyxml2::XMLDocument doc;
  if (doc.Parse(sv.data(), sv.size()) != tinyxml2::XML_SUCCESS) {
    throw std::runtime_error("Invalid XML");
  }

  const tinyxml2::XMLElement *info = doc.FirstChildElement("ExamInformation");
  if (!info) {
    throw std::runtime_error("Missing <ExamInformation>");
  }

  // ---- ExamInformation attributes ----
  tc.Name = attr_str(info, "Name");
  tc.InputFile = attr_str(info, "InputFile");
  tc.OutputFile = attr_str(info, "OutputFile");
  tc.UseStdIn = attr_bool(info, "UseStdIn");
  tc.UseStdOut = attr_bool(info, "UseStdOut");
  tc.EvaluatorName = attr_str(info, "EvaluatorName");
  tc.Mark = attr_float(info, "Mark");
  tc.TimeLimit = attr_float(info, "TimeLimit");
  tc.MemoryLimit = attr_int(info, "MemoryLimit");

  // ---- Sub test cases ----
  for (const tinyxml2::XMLElement *e = info->FirstChildElement("TestCase"); e;
       e = e->NextSiblingElement("TestCase")) {

    Subtest st;
    st.Name = attr_str(e, "Name");
    st.Mark = attr_float(e, "Mark");
    st.TimeLimit = attr_float(e, "TimeLimit");
    st.MemoryLimit = attr_int(e, "MemoryLimit");

    tc.subtests.push_back(std::move(st));
  }
}
template <>
void ParseTestSettings<ParseType::JSON>(const std::string_view &sv,
                                        Testcases &tc) {
  /*e.g.
  ```json
  {
          "ExamInformation": {
                  "TestCase": {
                          "Name": "00265ae7f658443a9fdd4e6b74562bf6",
                          "Mark": "-1",
                          "TimeLimit": "-1",
                          "MemoryLimit": "-1"
                  },
                  "Name": "CBAI3",
                  "InputFile": "CBAI3.INP",
                  "UseStdIn": "false",
                  "OutputFile": "CBAI3.OUT",
                  "UseStdOut": "false",
                  "EvaluatorName": "C1LinesWordsIgnoreCase.dll",
                  "Mark": "1",
                  "TimeLimit": "1",
                  "MemoryLimit": "1024"
          }
  }
  ```
  */
  auto j = nlohmann::json::parse(sv);

  if (!j.is_object() || !j.contains("ExamInformation"))
    throw std::runtime_error("Missing ExamInformation");
  const auto &info = j.at("ExamInformation");
  if (!info.is_object())
    throw std::runtime_error("Bad ExamInformation");

  tc.Name = json_str(info, "Name");
  tc.InputFile = json_str(info, "InputFile");
  tc.OutputFile = json_str(info, "OutputFile");
  tc.UseStdIn = json_bool(info, "UseStdIn", false);
  tc.UseStdOut = json_bool(info, "UseStdOut", false);
  tc.Mark = json_float(info, "Mark", 1.0f);
  tc.TimeLimit = json_float(info, "TimeLimit", 1.0f);
  tc.MemoryLimit = json_int(info, "MemoryLimit", 1024);
  tc.EvaluatorName = json_str(info, "EvaluatorName");
  if (tc.Name.empty() || tc.EvaluatorName.empty())
    throw std::runtime_error("Missing required ExamInformation fields");

  auto one_subtest = [&](const nlohmann::json &t) {
    if (!t.is_object())
      throw std::runtime_error("Bad TestCase entry");
    Subtest st;
    st.Name = json_str(t, "Name");
    st.Mark = json_float(t, "Mark", -1.0f);
    st.TimeLimit = json_float(t, "TimeLimit", -1.0f);
    st.MemoryLimit = json_int(t, "MemoryLimit", -1);
    tc.subtests.push_back(st);
  };
  if (info.contains("TestCase")) {
    const auto &t = info.at("TestCase");
    if (t.is_array()) {
      for (const auto &n : t)
        one_subtest(n);
    } else {
      one_subtest(t); // single-object form
    }
  }
}

template <>
void ParseTestSettings<ParseType::TOML>(const std::string_view &sv,
                                        Testcases &tc) {
  /*e.g. (note: attributes don't exist in TOML as it's dead simple but well
  defined)
  ```toml
  [ExamInformation]
  Name = "CBAI3"
  InputFile = "CBAI3.INP"
  UseStdIn = false
  OutputFile = "CBAI3.OUT"
  UseStdOut = false
  EvaluatorName = "C1LinesWordsIgnoreCase.dll"
  Mark = 1
  TimeLimit = 1
  MemoryLimit = 1024

  [[ExamInformation.TestCase]]
  Name = "00265ae7f658443a9fdd4e6b74562bf6"
  Mark = -1
  TimeLimit = -1
  MemoryLimit = -1
  ```
  */
  toml::table tbl = toml::parse(sv);

  auto info = tbl["ExamInformation"].as_table();
  if (!info)
    throw std::runtime_error("Missing [ExamInformation]");

  tc.Name = toml_req_str(*info, "Name");
  tc.InputFile = toml_req_str(*info, "InputFile");
  tc.OutputFile = toml_req_str(*info, "OutputFile");
  tc.UseStdIn = toml_req_bool(*info, "UseStdIn");
  tc.UseStdOut = toml_req_bool(*info, "UseStdOut");
  tc.Mark = static_cast<float>(toml_req_i64(*info, "Mark"));
  tc.TimeLimit = static_cast<float>(toml_req_i64(*info, "TimeLimit"));
  tc.MemoryLimit = static_cast<int>(toml_req_i64(*info, "MemoryLimit"));
  tc.EvaluatorName = toml_req_str(*info, "EvaluatorName");

  if (const toml::node *tc_node = info->get("TestCase")) {
    if (auto arr = tc_node->as_array()) {
      for (const auto &n : *arr) {
        const auto *t = n.as_table();
        if (!t)
          continue;

        // Tolerant accessors: missing keys inherit (-1/"").
        auto get_str = [&](const char *key) -> std::string {
          const toml::node *kn = t->get(key);
          if (!kn)
            return "";
          if (auto v = kn->value<std::string>())
            return *v;
          return "";
        };
        auto get_i64 = [&](const char *key) -> int64_t {
          const toml::node *kn = t->get(key);
          if (!kn)
            return -1;
          if (auto v = toml_opt_i64(*kn))
            return *v;
          return -1;
        };

        Subtest st;
        st.Name = get_str("Name");
        st.Mark = static_cast<float>(get_i64("Mark"));
        st.TimeLimit = static_cast<float>(get_i64("TimeLimit"));
        st.MemoryLimit = static_cast<int>(get_i64("MemoryLimit"));
        tc.subtests.push_back(std::move(st));
      }
    }
  }
}
template <>
void ParseGlobalOptions<ParseType::XML>(const std::string_view &xml,
                                        Configuration &out) {
  /*e.g.
  ```xml
  <ThemisConfiguration>
      <CompilerConfigurations Identifier="THEMISCompiler">
          <Item ext=".cpp"
              cmd="&quot;D:\llvm-mingw-20251216-ucrt-x86_64\bin\g++.exe&quot;
  -std=c++14 &quot;%NAME%%EXT%&quot; -pipe -O2 -s -static -lm -x c++
  -o&quot;%NAME%.exe&quot; -Wl,--stack,66060288 -Werror=pedantic
  -Werror=vla|@WorkDir=%PATH%" /> <Item ext=".c"
              cmd="&quot;D:\llvm-mingw-20251216-ucrt-x86_64\bin\gcc.exe&quot;
  -std=c11 &quot;%NAME%%EXT%&quot; -pipe -O2 -s -static -lm -x c
  -o&quot;%NAME%.exe&quot; -Wl,--stack,66060288 -Werror=pedantic
  -Werror=vla|@WorkDir=%PATH%" /> <Item ext=".pas" cmd="&quot;C:\Program Files
  (x86)\Themis\fpc\bin\i386-win32\fpc.exe&quot; -o&quot;%NAME%.exe&quot; -O2 -XS
  -Sg -Cs66060288 &quot;%NAME%%EXT%&quot;|@WorkDir=%PATH%" /> <Item ext=".pp"
              cmd="&quot;C:\Program Files
  (x86)\Themis\fpc\bin\i386-win32\fpc.exe&quot; -o&quot;%NAME%.exe&quot; -O2 -XS
  -Sg -Cs66060288 &quot;%NAME%%EXT%&quot;|@WorkDir=%PATH%" /> <Item ext=".java"
              cmd="&quot;E:\Program
  Files\Microsoft\jdk-25.0.1.8-hotspot\bin\javac.exe&quot;
  &quot;%NAME%%EXT%&quot;|@WorkDir=%PATH%" /> <Item ext=".exe" cmd=";Nếu không
  muốn dịch lại khi đã có file .exe, chuyển loại file này lên đầu" /> <Item
  ext=".class" cmd=";Nếu không muốn dịch lại khi đã có file .class, chuyển loại
  file này lên đầu" /> <Item ext=".py" cmd=";Mã nguồn Python được thông dịch!"
  />
      </CompilerConfigurations>
      <Environment Identifier="THEMISEnvironment" SubmitDir=""
  DecompressDir="C:\ProgramData\" ActiveSecurity="false"
  ContestHouse="C:\ProgramData\" AdminUserName="" AdminPassword=""
          AdminDomain="WINDOWS"
          LastExamDir="C:\Users\winapiadmin\Downloads\Compressed\New
  folder\tests" LastContestantDir="H:\De Hai phong 2025" ExamEditAction="0"
  ToolBarVisible="true" />
  </ThemisConfiguration>
  ```*/
  tinyxml2::XMLDocument doc;
  if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS)
    throw std::runtime_error("Not XML!");

  auto *root = doc.FirstChildElement("ThemisConfiguration");
  if (!root)
    root = doc.FirstChildElement("Configuration");
  if (!root)
    throw std::runtime_error("Missing <ThemisConfiguration>/<Configuration>");

  if (auto *cc = root->FirstChildElement("CompilerConfigurations")) {
    out.compiler.identifier =
        cc->Attribute("Identifier") ? cc->Attribute("Identifier") : "";

    for (auto *item = cc->FirstChildElement("Item"); item;
         item = item->NextSiblingElement("Item")) {

      CompilerItem ci;
      ci.ext = attr_str(item, "ext");
      ci.cmd = attr_str(item, "cmd");

      out.compiler.items.push_back(std::move(ci));
    }
  }

  if (auto *env = root->FirstChildElement("Environment")) {
    out.environment.identifier = attr_str(env, "Identifier");
    out.environment.submitDir = attr_str(env, "SubmitDir");
    out.environment.decompressDir = attr_str(env, "DecompressDir");
    out.environment.contestHouse = attr_str(env, "ContestHouse");
    out.environment.adminUserName = attr_str(env, "AdminUserName");
    out.environment.adminPassword = attr_str(env, "AdminPassword");
    out.environment.adminDomain = attr_str(env, "AdminDomain");
    out.environment.lastExamDir = attr_str(env, "LastExamDir");
    out.environment.lastContestantDir = attr_str(env, "LastContestantDir");
    env->QueryBoolAttribute("ActiveSecurity", &out.environment.activeSecurity);
    env->QueryIntAttribute("ExamEditAction", &out.environment.examEditAction);
    env->QueryBoolAttribute("ToolBarVisible", &out.environment.toolBarVisible);
  }
}

template <>
void ParseGlobalOptions<ParseType::YAML>(const std::string_view &sv,
                                         Configuration &out) {
  /*e.g.
  ```yaml
  ThemisConfiguration:
    CompilerConfigurations:
      Item:
        - ext: .cpp
          cmd: >-
            "D:\llvm-mingw-20251216-ucrt-x86_64\bin\g++.exe" -std=c++14
            "%NAME%%EXT%" -pipe -O2 -s -static -lm -x c++ -o"%NAME%.exe"
            -Wl,--stack,66060288 -Werror=pedantic -Werror=vla|@WorkDir=%PATH%
        - ext: .c
          cmd: >-
            "D:\llvm-mingw-20251216-ucrt-x86_64\bin\gcc.exe" -std=c11
            "%NAME%%EXT%" -pipe -O2 -s -static -lm -x c -o"%NAME%.exe"
            -Wl,--stack,66060288 -Werror=pedantic -Werror=vla|@WorkDir=%PATH%
        - ext: .pas
          cmd: >-
            "C:\Program Files (x86)\Themis\fpc\bin\i386-win32\fpc.exe"
            -o"%NAME%.exe" -O2 -XS -Sg -Cs66060288 "%NAME%%EXT%"|@WorkDir=%PATH%
        - ext: .pp
          cmd: >-
            "C:\Program Files (x86)\Themis\fpc\bin\i386-win32\fpc.exe"
            -o"%NAME%.exe" -O2 -XS -Sg -Cs66060288 "%NAME%%EXT%"|@WorkDir=%PATH%
        - ext: .java
          cmd: >-
            "E:\Program Files\Microsoft\jdk-25.0.1.8-hotspot\bin\javac.exe"
            "%NAME%%EXT%"|@WorkDir=%PATH%
        - ext: .exe
          cmd: >-
            ;Nếu không muốn dịch lại khi đã có file .exe, chuyển loại file này
  lên đầu
        - ext: .class
          cmd: >-
            ;Nếu không muốn dịch lại khi đã có file .class, chuyển loại file này
            lên đầu
        - ext: .py
          cmd: ;Mã nguồn Python được thông dịch!
      Identifier: THEMISCompiler
    Environment:
      Identifier: THEMISEnvironment
      SubmitDir: ''
      DecompressDir: 'C:\ProgramData\'
      ActiveSecurity: 'false'
      ContestHouse: 'C:\ProgramData\'
      AdminUserName: ''
      AdminPassword: ''
      AdminDomain: WINDOWS
      LastExamDir: 'C:\Users\winapiadmin\Downloads\Compressed\New folder\tests'
      LastContestantDir: 'H:\De Hai phong 2025'
      ExamEditAction: '0'
      ToolBarVisible: 'true'
  ```*/
  YAML::Node root = YAML::Load(std::string(sv));

  // Tolerant accessors: absent keys fall back to defaults instead of
  // throwing on undefined nodes.
  auto gs = [](const YAML::Node &n, const char *k) -> std::string {
    const YAML::Node v = n[k];
    if (!v)
      return "";
    try {
      return v.as<std::string>();
    } catch (...) {
      return "";
    }
  };
  auto gb = [](const YAML::Node &n, const char *k, bool def) -> bool {
    const YAML::Node v = n[k];
    if (!v)
      return def;
    try {
      return v.as<bool>();
    } catch (...) {
    }
    // Quoted booleans ('false') are strings to strict as<bool>.
    try {
      std::string s = lower_copy(v.as<std::string>());
      if (s == "true" || s == "1")
        return true;
      if (s == "false" || s == "0")
        return false;
    } catch (...) {
    }
    return def;
  };
  auto gi = [](const YAML::Node &n, const char *k, int def) -> int {
    const YAML::Node v = n[k];
    if (!v)
      return def;
    try {
      return v.as<int>();
    } catch (...) {
    }
    try {
      return std::stoi(v.as<std::string>());
    } catch (...) {
    }
    return def;
  };

  auto tc = root["ThemisConfiguration"];
  if (!tc)
    tc = root["Configuration"];
  if (!tc)
    throw std::runtime_error("Missing ThemisConfiguration/Configuration");
  auto cc = tc["CompilerConfigurations"];
  if (cc) {
    out.compiler.identifier = gs(cc, "Identifier");

    // NOTE: the key is "Item" (singular), matching the documented format.
    auto items = cc["Item"];
    if (items && items.IsSequence()) {
      for (auto item : items) {
        CompilerItem ci;
        ci.ext = gs(item, "ext");
        ci.cmd = gs(item, "cmd");
        out.compiler.items.push_back(std::move(ci));
      }
    }
  }

  auto env = tc["Environment"];
  if (env) {
    out.environment.identifier = gs(env, "Identifier");
    out.environment.submitDir = gs(env, "SubmitDir");
    out.environment.decompressDir = gs(env, "DecompressDir");
    out.environment.activeSecurity = gb(env, "ActiveSecurity", false);
    out.environment.contestHouse = gs(env, "ContestHouse");
    out.environment.adminUserName = gs(env, "AdminUserName");
    out.environment.adminPassword = gs(env, "AdminPassword");
    out.environment.adminDomain = gs(env, "AdminDomain");
    out.environment.lastExamDir = gs(env, "LastExamDir");
    out.environment.lastContestantDir = gs(env, "LastContestantDir");
    out.environment.examEditAction = gi(env, "ExamEditAction", 0);
    out.environment.toolBarVisible = gb(env, "ToolBarVisible", false);
  }
}

template <>
void ParseGlobalOptions<ParseType::TOML>(const std::string_view &sv,
                                         Configuration &out) {
  /*e.g.
  [ThemisConfiguration]

  [[ThemisConfiguration.CompilerConfigurations]]
  Identifier = "THEMISCompiler"

  [[ThemisConfiguration.CompilerConfigurations.Item]]
  ext = ".cpp"
  cmd = "\"D:\\llvm-mingw-20251216-ucrt-x86_64\\bin\\g++.exe\" -std=c++14
  \"%NAME%%EXT%\" -pipe -O2 -s -static -lm -x c++ -o\"%NAME%.exe\"
  -Wl,--stack,66060288 -Werror=pedantic -Werror=vla|@WorkDir=%PATH%"

  [[ThemisConfiguration.CompilerConfigurations.Item]]
  ext = ".c"
  cmd = "\"D:\\llvm-mingw-20251216-ucrt-x86_64\\bin\\gcc.exe\" -std=c11
  \"%NAME%%EXT%\" -pipe -O2 -s -static -lm -x c -o\"%NAME%.exe\"
  -Wl,--stack,66060288 -Werror=pedantic -Werror=vla|@WorkDir=%PATH%"

  [[ThemisConfiguration.CompilerConfigurations.Item]]
  ext = ".pas"
  cmd = "\"C:\\Program Files (x86)\\Themis\\fpc\\bin\\i386-win32\\fpc.exe\"
  -o\"%NAME%.exe\" -O2 -XS -Sg -Cs66060288 \"%NAME%%EXT%\"|@WorkDir=%PATH%"

  [[ThemisConfiguration.CompilerConfigurations.Item]]
  ext = ".pp"
  cmd = "\"C:\\Program Files (x86)\\Themis\\fpc\\bin\\i386-win32\\fpc.exe\"
  -o\"%NAME%.exe\" -O2 -XS -Sg -Cs66060288 \"%NAME%%EXT%\"|@WorkDir=%PATH%"

  [[ThemisConfiguration.CompilerConfigurations.Item]]
  ext = ".java"
  cmd = "\"E:\\Program Files\\Microsoft\\jdk-25.0.1.8-hotspot\\bin\\javac.exe\"
  \"%NAME%%EXT%\"|@WorkDir=%PATH%"

  [[ThemisConfiguration.CompilerConfigurations.Item]]
  ext = ".exe"
  cmd = ";Nếu không muốn dịch lại khi đã có file .exe, chuyển loại file này lên
  đầu"

  [[ThemisConfiguration.CompilerConfigurations.Item]]
  ext = ".class"
  cmd = ";Nếu không muốn dịch lại khi đã có file .class, chuyển loại file này
  lên đầu"

  [[ThemisConfiguration.CompilerConfigurations.Item]]
  ext = ".py"
  cmd = ";Mã nguồn Python được thông dịch!"


  [[ThemisConfiguration.Environment]]
  Identifier = "THEMISEnvironment"
  SubmitDir = ""
  DecompressDir = "C:\\ProgramData\\"
  ActiveSecurity = false
  ContestHouse = "C:\\ProgramData\\"
  AdminUserName = ""
  AdminPassword = ""
  AdminDomain = "WINDOWS"
  LastExamDir = "C:\\Users\\winapiadmin\\Downloads\\Compressed\\New
  folder\\tests" LastContestantDir = "H:\\De Hai phong 2025" ExamEditAction = 0
  ToolBarVisible = true
  */
  auto tree = toml::parse(sv);

  const auto *config = tree.get_as<toml::table>("ThemisConfiguration");
  if (!config)
    config = tree.get_as<toml::table>("Configuration");
  if (!config)
    throw std::runtime_error("Missing [ThemisConfiguration]");

  // CompilerConfigurations

  if (auto arr = config->get_as<toml::array>("CompilerConfigurations")) {
    for (const auto &node : *arr) {
      const auto &tbl = *node.as_table();
      CompilerConfiguration cc;

      cc.identifier = tbl.at("Identifier").value_or("");

      if (auto items = tbl.get_as<toml::array>("Item")) {
        for (const auto &it : *items) {
          const auto &itbl = *it.as_table();
          CompilerItem item;

          item.ext = itbl.at("ext").value_or("");
          item.cmd = itbl.at("cmd").value_or("");

          cc.items.push_back(std::move(item));
        }
      }

      out.compiler.items.insert(out.compiler.items.end(), cc.items.begin(),
                                cc.items.end());
    }
  }

  // Environment

  if (auto arr = config->get_as<toml::array>("Environment")) {
    for (const auto &node : *arr) {
      const auto &tbl = *node.as_table();
      Environment env;

      env.identifier = tbl.at("Identifier").value_or("");
      env.submitDir = tbl.at("SubmitDir").value_or("");
      env.decompressDir = tbl.at("DecompressDir").value_or("");
      env.activeSecurity = tbl.at("ActiveSecurity").value_or(false);
      env.contestHouse = tbl.at("ContestHouse").value_or("");
      env.adminUserName = tbl.at("AdminUserName").value_or("");
      env.adminPassword = tbl.at("AdminPassword").value_or("");
      env.adminDomain = tbl.at("AdminDomain").value_or("");
      env.lastExamDir = tbl.at("LastExamDir").value_or("");
      env.lastContestantDir = tbl.at("LastContestantDir").value_or("");
      env.examEditAction = tbl.at("ExamEditAction").value_or(0);
      env.toolBarVisible = tbl.at("ToolBarVisible").value_or(true);

      out.environment = env;
    }
  }
}
template <>
void ParseGlobalOptions<ParseType::JSON>(const std::string_view &text,
                                         Configuration &out) {
  /*e.g.
  {
    "ThemisConfiguration": {
      "CompilerConfigurations": {
        "Identifier": "THEMISCompiler",
        "Item": [
          {
            "ext": ".cpp",
            "cmd": "\"D:\\llvm-mingw-20251216-ucrt-x86_64\\bin\\g++.exe\"
  -std=c++14 \"%NAME%%EXT%\" -pipe -O2 -s -static -lm -x c++ -o\"%NAME%.exe\"
  -Wl,--stack,66060288 -Werror=pedantic -Werror=vla|@WorkDir=%PATH%"
          },
          {
            "ext": ".c",
            "cmd": "\"D:\\llvm-mingw-20251216-ucrt-x86_64\\bin\\gcc.exe\"
  -std=c11 \"%NAME%%EXT%\" -pipe -O2 -s -static -lm -x c -o\"%NAME%.exe\"
  -Wl,--stack,66060288 -Werror=pedantic -Werror=vla|@WorkDir=%PATH%"
          },
          {
            "ext": ".pas",
            "cmd": "\"C:\\Program Files
  (x86)\\Themis\\fpc\\bin\\i386-win32\\fpc.exe\" -o\"%NAME%.exe\" -O2 -XS -Sg
  -Cs66060288 \"%NAME%%EXT%\"|@WorkDir=%PATH%"
          },
          {
            "ext": ".pp",
            "cmd": "\"C:\\Program Files
  (x86)\\Themis\\fpc\\bin\\i386-win32\\fpc.exe\" -o\"%NAME%.exe\" -O2 -XS -Sg
  -Cs66060288 \"%NAME%%EXT%\"|@WorkDir=%PATH%"
          },
          {
            "ext": ".java",
            "cmd": "\"E:\\Program
  Files\\Microsoft\\jdk-25.0.1.8-hotspot\\bin\\javac.exe\"
  \"%NAME%%EXT%\"|@WorkDir=%PATH%"
          },
          {
            "ext": ".exe",
            "cmd": ";Nếu không muốn dịch lại khi đã có file .exe, chuyển loại
  file này lên đầu"
          },
          {
            "ext": ".class",
            "cmd": ";Nếu không muốn dịch lại khi đã có file .class, chuyển loại
  file này lên đầu"
          },
          {
            "ext": ".py",
            "cmd": ";Mã nguồn Python được thông dịch!"
          }
        ]
      },
      "Environment": {
        "Identifier": "THEMISEnvironment",
        "SubmitDir": "",
        "DecompressDir": "C:\\ProgramData\\",
        "ActiveSecurity": false,
        "ContestHouse": "C:\\ProgramData\\",
        "AdminUserName": "",
        "AdminPassword": "",
        "AdminDomain": "WINDOWS",
        "LastExamDir": "C:\\Users\\winapiadmin\\Downloads\\Compressed\\New
  folder\\tests", "LastContestantDir": "H:\\De Hai phong 2025",
        "ExamEditAction": 0,
        "ToolBarVisible": true
      }
    }
  }
  */
  nlohmann::json j = nlohmann::json::parse(text);
  if (!j.is_object())
    throw std::runtime_error("Not a global configuration document");

  // Accept the documented wrapper as well as bare sections.
  const nlohmann::json *root = &j;
  if (j.contains("ThemisConfiguration") && j["ThemisConfiguration"].is_object())
    root = &j["ThemisConfiguration"];
  else if (j.contains("Configuration") && j["Configuration"].is_object())
    root = &j["Configuration"];
  const auto &cfg = *root;

  if (!cfg.contains("CompilerConfigurations") && !cfg.contains("Environment"))
    throw std::runtime_error("Not a global configuration document");

  // CompilerConfigurations
  if (cfg.contains("CompilerConfigurations") &&
      cfg["CompilerConfigurations"].is_object()) {
    const auto &cc = cfg["CompilerConfigurations"];

    out.compiler.identifier = json_str(cc, "Identifier");

    out.compiler.items.clear();

    if (cc.contains("Item") && cc["Item"].is_array()) {
      for (const auto &item : cc["Item"]) {
        if (!item.is_object())
          continue;
        CompilerItem ci;
        ci.ext = json_str(item, "ext");
        ci.cmd = json_str(item, "cmd");

        out.compiler.items.push_back(std::move(ci));
      }
    }
  }

  // Environment
  if (cfg.contains("Environment") && cfg["Environment"].is_object()) {
    const auto &env = cfg["Environment"];
    auto &e = out.environment;

    e.identifier = json_str(env, "Identifier");
    e.submitDir = json_str(env, "SubmitDir");
    e.decompressDir = json_str(env, "DecompressDir");
    e.contestHouse = json_str(env, "ContestHouse");
    e.adminUserName = json_str(env, "AdminUserName");
    e.adminPassword = json_str(env, "AdminPassword");
    e.adminDomain = json_str(env, "AdminDomain");
    e.lastExamDir = json_str(env, "LastExamDir");
    e.lastContestantDir = json_str(env, "LastContestantDir");

    e.activeSecurity = json_bool(env, "ActiveSecurity", false);
    e.examEditAction = json_int(env, "ExamEditAction", 0);
    e.toolBarVisible = json_bool(env, "ToolBarVisible", true);
  }
}

template <typename Fn> static bool try_parse(const std::string_view sv, Fn fn) {
  try {
    fn(sv);
    return true;
  } catch (...) {
  }
  return false;
}

void parseGlobalSettingsFormat(const std::string_view sv, Configuration &tc) {
  // Each attempt parses into a temporary: a failed parser must not leave
  // partial items behind for the successful one to inherit. Trial order is
  // sniffed ('<' XML, '{' JSON) because every JSON document is also valid
  // YAML and would otherwise be misread by the YAML parser first.
  using Fn = void (*)(const std::string_view &, Configuration &);
  std::vector<Fn> order;
  switch (sniff_format(sv)) {
  case '<':
    order = {&ParseGlobalOptions<ParseType::XML>,
             &ParseGlobalOptions<ParseType::YAML>,
             &ParseGlobalOptions<ParseType::JSON>,
             &ParseGlobalOptions<ParseType::TOML>};
    break;
  case '{':
    order = {&ParseGlobalOptions<ParseType::JSON>,
             &ParseGlobalOptions<ParseType::YAML>,
             &ParseGlobalOptions<ParseType::XML>,
             &ParseGlobalOptions<ParseType::TOML>};
    break;
  default:
    order = {&ParseGlobalOptions<ParseType::YAML>,
             &ParseGlobalOptions<ParseType::TOML>,
             &ParseGlobalOptions<ParseType::JSON>,
             &ParseGlobalOptions<ParseType::XML>};
    break;
  }
  for (Fn f : order) {
    Configuration tmp;
    if (try_parse(sv, [&](std::string_view s) { f(s, tmp); })) {
      tc = std::move(tmp);
      return;
    }
  }
  throw std::runtime_error("File format not implemented");
}

void parseSettingsFormat(const std::string_view sv, Testcases &tc) {
  using Fn = void (*)(const std::string_view &, Testcases &);
  std::vector<Fn> order;
  switch (sniff_format(sv)) {
  case '<':
    order = {&ParseTestSettings<ParseType::XML>,
             &ParseTestSettings<ParseType::YAML>,
             &ParseTestSettings<ParseType::JSON>,
             &ParseTestSettings<ParseType::TOML>};
    break;
  case '{':
    order = {&ParseTestSettings<ParseType::JSON>,
             &ParseTestSettings<ParseType::YAML>,
             &ParseTestSettings<ParseType::XML>,
             &ParseTestSettings<ParseType::TOML>};
    break;
  default:
    order = {&ParseTestSettings<ParseType::YAML>,
             &ParseTestSettings<ParseType::TOML>,
             &ParseTestSettings<ParseType::JSON>,
             &ParseTestSettings<ParseType::XML>};
    break;
  }
  for (Fn f : order) {
    Testcases tmp;
    if (try_parse(sv, [&](std::string_view s) { f(s, tmp); })) {
      tc = std::move(tmp);
      return;
    }
  }
  throw std::runtime_error("File format not implemented");
}
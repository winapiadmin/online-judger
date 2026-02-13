#include "Parsers.h"
#include <yaml-cpp/yaml.h>
#include <tinyxml2.h>
#include <nlohmann/json.hpp>
#include <toml++/toml.h>
using namespace std;
template<>
void ParseTestSettings<ParseType::YAML>(const std::string_view &sv, Testcases &tc){
  YAML::Node node=YAML::Load(std::string(sv)), info=node["ExamInformation"], subtestcases=info["TestCase"];
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
  tc.Name=info["Name"].as<string>();
  tc.InputFile=info["InputFile"].as<string>();
  tc.OutputFile=info["OutputFile"].as<string>();
  tc.UseStdIn=info["UseStdIn"].as<bool>();
  tc.UseStdOut=info["UseStdOut"].as<bool>();
  tc.Mark=info["Mark"].as<float>();
  tc.MemoryLimit=info["MemoryLimit"].as<int>();
  tc.TimeLimit=info["TimeLimit"].as<float>();
  tc.EvaluatorName=info["EvaluatorName"].as<string>();
  if (subtestcases  && subtestcases.IsSequence())
    for (YAML::Node test:subtestcases){
      Subtest _test;
      _test.Name=test["Name"].as<string>();
      _test.Mark=test["Mark"].as<float>();
      _test.TimeLimit=test["TimeLimit"].as<float>();
      _test.MemoryLimit=test["MemoryLimit"].as<int>();
      tc.subtests.push_back(std::move(_test));
    }
}
namespace {
  // impl detail for XML low-level parsing
    
  static bool attr_bool(const tinyxml2::XMLElement* e, const char* name, bool def = false) {
      if (const char* v = e->Attribute(name))
          return strcmp(v, "true") == 0 || strcmp(v, "1") == 0;
      return def;
  }

  static int attr_int(const tinyxml2::XMLElement* e, const char* name, int def = 0) {
      e->QueryIntAttribute(name, &def);
      return def;
  }

  static float attr_float(const tinyxml2::XMLElement* e, const char* name, float def = 0.f) {
      e->QueryFloatAttribute(name, &def);
      return def;
  }

  static std::string attr_str(const tinyxml2::XMLElement* e, const char* name) {
      const char* v = e->Attribute(name);
      return v ? v : "";
  }
}
template<>
void ParseTestSettings<ParseType::XML>(const std::string_view& sv, Testcases& tc){
    /*
e.g.
```xml
<ExamInformation Name="CBAI3" InputFile="CBAI3.INP" UseStdIn="false" OutputFile="CBAI3.OUT"
    UseStdOut="false" EvaluatorName="C1LinesWordsIgnoreCase.dll" Mark="1" TimeLimit="1" MemoryLimit="1024">
<TestCase Name="00265ae7f658443a9fdd4e6b74562bf6" Mark="-1" TimeLimit="-1" MemoryLimit="-1"/>
</ExamInformation>
```*/
  tinyxml2::XMLDocument doc;
  if (doc.Parse(sv.data(), sv.size()) != tinyxml2::XML_SUCCESS) {
      throw std::runtime_error("Invalid XML");
  }

  const tinyxml2::XMLElement* info = doc.FirstChildElement("ExamInformation");
  if (!info) {
      throw std::runtime_error("Missing <ExamInformation>");
  }

  // ---- ExamInformation attributes ----
  tc.Name          = attr_str(info, "Name");
  tc.InputFile     = attr_str(info, "InputFile");
  tc.OutputFile    = attr_str(info, "OutputFile");
  tc.UseStdIn      = attr_bool(info, "UseStdIn");
  tc.UseStdOut     = attr_bool(info, "UseStdOut");
  tc.EvaluatorName = attr_str(info, "EvaluatorName");
  tc.Mark          = attr_float(info, "Mark");
  tc.TimeLimit     = attr_float(info, "TimeLimit");
  tc.MemoryLimit   = attr_int(info, "MemoryLimit");

  // ---- Sub test cases ----
  for (const tinyxml2::XMLElement* e = info->FirstChildElement("TestCase");
        e;
        e = e->NextSiblingElement("TestCase")) {

      Subtest st;
      st.Name        = attr_str(e, "Name");
      st.Mark        = attr_float(e, "Mark");
      st.TimeLimit   = attr_float(e, "TimeLimit");
      st.MemoryLimit = attr_int(e, "MemoryLimit");

      tc.subtests.push_back(std::move(st));
  }
}
template<>
void ParseTestSettings<ParseType::JSON>(const std::string_view& sv, Testcases& tc){
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

    auto& info = j.at("ExamInformation");

    tc.Name           = info.at("Name").get<std::string>();
    tc.InputFile      = info.at("InputFile").get<std::string>();
    tc.OutputFile     = info.at("OutputFile").get<std::string>();
    tc.UseStdIn       = info.at("UseStdIn").get<bool>();
    tc.UseStdOut      = info.at("UseStdOut").get<bool>();
    tc.Mark           = info.at("Mark").get<float>();
    tc.TimeLimit      = info.at("TimeLimit").get<float>();
    tc.MemoryLimit    = info.at("MemoryLimit").get<int>();
    tc.EvaluatorName  = info.at("EvaluatorName").get<std::string>();

    if (info.contains("TestCase")) {
        const auto& t = info.at("TestCase");
        Subtest st;
        st.Name        = t.at("Name").get<std::string>();
        st.Mark        = t.at("Mark").get<float>();
        st.TimeLimit   = t.at("TimeLimit").get<float>();
        st.MemoryLimit = t.at("MemoryLimit").get<int>();
        tc.subtests.push_back(st);
    }
}

template<>
void ParseTestSettings<ParseType::TOML>(const std::string_view& sv, Testcases& tc){
/*e.g. (note: attributes don't exist in TOML as it's dead simple but well defined)
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

  auto req = [&](const char* key) -> const toml::node& {
      const toml::node* n = info->get(key);
      if (!n)
          throw std::runtime_error(std::string("Missing key: ") + key);
      return *n;
  };

  tc.Name          = req("Name").value<std::string>().value();
  tc.InputFile     = req("InputFile").value<std::string>().value();
  tc.OutputFile    = req("OutputFile").value<std::string>().value();
  tc.UseStdIn      = req("UseStdIn").value<bool>().value();
  tc.UseStdOut     = req("UseStdOut").value<bool>().value();
  tc.Mark          = static_cast<float>(req("Mark").value<int64_t>().value());
  tc.TimeLimit     = static_cast<float>(req("TimeLimit").value<int64_t>().value());
  tc.MemoryLimit   = static_cast<int>(req("MemoryLimit").value<int64_t>().value());
  tc.EvaluatorName = req("EvaluatorName").value<std::string>().value();

  if (auto arr = info->get("TestCase")->as_array()) {
      for (const auto& n : *arr) {
          auto t = n.as_table();
          if (!t) continue;

          Subtest st;
          st.Name        = t->get("Name")->value<std::string>().value();
          st.Mark        = static_cast<float>(t->get("Mark")->value<int64_t>().value());
          st.TimeLimit   = static_cast<float>(t->get("TimeLimit")->value<int64_t>().value());
          st.MemoryLimit = static_cast<int>(t->get("MemoryLimit")->value<int64_t>().value());
          tc.subtests.push_back(st);
      }
  }
}
template<>
void ParseGlobalOptions<ParseType::XML>(const std::string_view& xml, Configuration& out) {
/*e.g.
```xml
<ThemisConfiguration>
    <CompilerConfigurations Identifier="THEMISCompiler">
        <Item ext=".cpp"
            cmd="&quot;D:\llvm-mingw-20251216-ucrt-x86_64\bin\g++.exe&quot; -std=c++14 &quot;%NAME%%EXT%&quot; -pipe -O2 -s -static -lm -x c++ -o&quot;%NAME%.exe&quot; -Wl,--stack,66060288 -Werror=pedantic -Werror=vla|@WorkDir=%PATH%" />
        <Item ext=".c"
            cmd="&quot;D:\llvm-mingw-20251216-ucrt-x86_64\bin\gcc.exe&quot; -std=c11 &quot;%NAME%%EXT%&quot; -pipe -O2 -s -static -lm -x c -o&quot;%NAME%.exe&quot; -Wl,--stack,66060288 -Werror=pedantic -Werror=vla|@WorkDir=%PATH%" />
        <Item ext=".pas"
            cmd="&quot;C:\Program Files (x86)\Themis\fpc\bin\i386-win32\fpc.exe&quot; -o&quot;%NAME%.exe&quot; -O2 -XS -Sg -Cs66060288 &quot;%NAME%%EXT%&quot;|@WorkDir=%PATH%" />
        <Item ext=".pp"
            cmd="&quot;C:\Program Files (x86)\Themis\fpc\bin\i386-win32\fpc.exe&quot; -o&quot;%NAME%.exe&quot; -O2 -XS -Sg -Cs66060288 &quot;%NAME%%EXT%&quot;|@WorkDir=%PATH%" />
        <Item ext=".java"
            cmd="&quot;E:\Program Files\Microsoft\jdk-25.0.1.8-hotspot\bin\javac.exe&quot; &quot;%NAME%%EXT%&quot;|@WorkDir=%PATH%" />
        <Item ext=".exe"
            cmd=";Nếu không muốn dịch lại khi đã có file .exe, chuyển loại file này lên đầu" />
        <Item ext=".class"
            cmd=";Nếu không muốn dịch lại khi đã có file .class, chuyển loại file này lên đầu" />
        <Item ext=".py" cmd=";Mã nguồn Python được thông dịch!" />
    </CompilerConfigurations>
    <Environment Identifier="THEMISEnvironment" SubmitDir="" DecompressDir="C:\ProgramData\"
        ActiveSecurity="false" ContestHouse="C:\ProgramData\" AdminUserName="" AdminPassword=""
        AdminDomain="WINDOWS"
        LastExamDir="C:\Users\winapiadmin\Downloads\Compressed\New folder\tests"
        LastContestantDir="H:\De Hai phong 2025" ExamEditAction="0" ToolBarVisible="true" />
</ThemisConfiguration>
```*/
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS)
        throw std::runtime_error("Not XML!");

    auto* root = doc.FirstChildElement("ThemisConfiguration");
    if (!root) root = doc.FirstChildElement("Configuration");
    if (!root) throw std::runtime_error("Missing <ThemisConfiguration>/<Configuration>");

    if (auto* cc = root->FirstChildElement("CompilerConfigurations")) {
        out.compiler.identifier =
            cc->Attribute("Identifier") ? cc->Attribute("Identifier") : "";

        for (auto* item = cc->FirstChildElement("Item");
             item;
             item = item->NextSiblingElement("Item")) {

            CompilerItem ci;
            ci.ext = attr_str(item, "ext");
            ci.cmd = attr_str(item, "cmd");

            out.compiler.items.push_back(std::move(ci));
        }
    }

    if (auto* env = root->FirstChildElement("Environment")) {
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

template<>
void ParseGlobalOptions<ParseType::YAML>(const std::string_view& sv, Configuration& out) {
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
          ;Nếu không muốn dịch lại khi đã có file .exe, chuyển loại file này lên
          đầu
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

    auto tc = root["ThemisConfiguration"];
    if (!tc) tc=root["Configuration"];
    if (!tc) throw std::runtime_error("Missing ThemisConfiguration/Configuration");
    auto cc = tc["CompilerConfigurations"];
    if (cc) {
        out.compiler.identifier = cc["Identifier"].as<std::string>();

        auto items = cc["Items"];
        if (items && items.IsSequence()) {
            for (auto item : items) {
                CompilerItem ci;
                ci.ext = item["ext"].as<std::string>();
                ci.cmd = item["cmd"].as<std::string>();
                out.compiler.items.push_back(std::move(ci));
            }
        }
    }

    auto env = tc["Environment"];
    if (env) {
        out.environment.identifier = env["Identifier"].as<std::string>();
        out.environment.submitDir = env["SubmitDir"].as<std::string>();
        out.environment.decompressDir = env["DecompressDir"].as<std::string>();
        out.environment.activeSecurity = env["ActiveSecurity"].as<bool>();
        out.environment.contestHouse = env["ContestHouse"].as<std::string>();
        out.environment.adminUserName = env["AdminUserName"].as<std::string>();
        out.environment.adminPassword = env["AdminPassword"].as<std::string>();
        out.environment.adminDomain = env["AdminDomain"].as<std::string>();
        out.environment.lastExamDir = env["LastExamDir"].as<std::string>();
        out.environment.lastContestantDir = env["LastContestantDir"].as<std::string>();
        out.environment.examEditAction = env["ExamEditAction"].as<int>();
        out.environment.toolBarVisible = env["ToolBarVisible"].as<bool>();
    }
}

template<>
void ParseGlobalOptions<ParseType::TOML>(const std::string_view& sv, Configuration& out){
/*e.g.
[ThemisConfiguration]

[[ThemisConfiguration.CompilerConfigurations]]
Identifier = "THEMISCompiler"

[[ThemisConfiguration.CompilerConfigurations.Item]]
ext = ".cpp"
cmd = "\"D:\\llvm-mingw-20251216-ucrt-x86_64\\bin\\g++.exe\" -std=c++14 \"%NAME%%EXT%\" -pipe -O2 -s -static -lm -x c++ -o\"%NAME%.exe\" -Wl,--stack,66060288 -Werror=pedantic -Werror=vla|@WorkDir=%PATH%"

[[ThemisConfiguration.CompilerConfigurations.Item]]
ext = ".c"
cmd = "\"D:\\llvm-mingw-20251216-ucrt-x86_64\\bin\\gcc.exe\" -std=c11 \"%NAME%%EXT%\" -pipe -O2 -s -static -lm -x c -o\"%NAME%.exe\" -Wl,--stack,66060288 -Werror=pedantic -Werror=vla|@WorkDir=%PATH%"

[[ThemisConfiguration.CompilerConfigurations.Item]]
ext = ".pas"
cmd = "\"C:\\Program Files (x86)\\Themis\\fpc\\bin\\i386-win32\\fpc.exe\" -o\"%NAME%.exe\" -O2 -XS -Sg -Cs66060288 \"%NAME%%EXT%\"|@WorkDir=%PATH%"

[[ThemisConfiguration.CompilerConfigurations.Item]]
ext = ".pp"
cmd = "\"C:\\Program Files (x86)\\Themis\\fpc\\bin\\i386-win32\\fpc.exe\" -o\"%NAME%.exe\" -O2 -XS -Sg -Cs66060288 \"%NAME%%EXT%\"|@WorkDir=%PATH%"

[[ThemisConfiguration.CompilerConfigurations.Item]]
ext = ".java"
cmd = "\"E:\\Program Files\\Microsoft\\jdk-25.0.1.8-hotspot\\bin\\javac.exe\" \"%NAME%%EXT%\"|@WorkDir=%PATH%"

[[ThemisConfiguration.CompilerConfigurations.Item]]
ext = ".exe"
cmd = ";Nếu không muốn dịch lại khi đã có file .exe, chuyển loại file này lên đầu"

[[ThemisConfiguration.CompilerConfigurations.Item]]
ext = ".class"
cmd = ";Nếu không muốn dịch lại khi đã có file .class, chuyển loại file này lên đầu"

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
LastExamDir = "C:\\Users\\winapiadmin\\Downloads\\Compressed\\New folder\\tests"
LastContestantDir = "H:\\De Hai phong 2025"
ExamEditAction = 0
ToolBarVisible = true
*/
    auto tree=toml::parse(sv);

    const auto* config = tree.get_as<toml::table>("ThemisConfiguration");
    if (!config) config = tree.get_as<toml::table>("Configuration");
    if (!config)
        throw std::runtime_error("Missing [ThemisConfiguration]");

    // CompilerConfigurations

    if (auto arr = config->get_as<toml::array>("CompilerConfigurations"))
    {
        for (const auto& node : *arr)
        {
            const auto& tbl = *node.as_table();
            CompilerConfiguration cc;

            cc.identifier = tbl.at("Identifier").value_or("");

            if (auto items = tbl.get_as<toml::array>("Item"))
            {
                for (const auto& it : *items)
                {
                    const auto& itbl = *it.as_table();
                    CompilerItem item;

                    item.ext = itbl.at("ext").value_or("");
                    item.cmd = itbl.at("cmd").value_or("");

                    cc.items.push_back(std::move(item));
                }
            }

            out.compiler.items.insert(out.compiler.items.end(), cc.items.begin(), cc.items.end());
        }
    }

    // Environment

    if (auto arr = config->get_as<toml::array>("Environment"))
    {
        for (const auto& node : *arr)
        {
            const auto& tbl = *node.as_table();
            Environment env;

            env.identifier         = tbl.at("Identifier").value_or("");
            env.submitDir          = tbl.at("SubmitDir").value_or("");
            env.decompressDir      = tbl.at("DecompressDir").value_or("");
            env.activeSecurity     = tbl.at("ActiveSecurity").value_or(false);
            env.contestHouse       = tbl.at("ContestHouse").value_or("");
            env.adminUserName      = tbl.at("AdminUserName").value_or("");
            env.adminPassword      = tbl.at("AdminPassword").value_or("");
            env.adminDomain        = tbl.at("AdminDomain").value_or("");
            env.lastExamDir        = tbl.at("LastExamDir").value_or("");
            env.lastContestantDir  = tbl.at("LastContestantDir").value_or("");
            env.examEditAction     = tbl.at("ExamEditAction").value_or(0);
            env.toolBarVisible     = tbl.at("ToolBarVisible").value_or(true);

            out.environment = env;
        }
    }
}
template<>
void ParseGlobalOptions<ParseType::JSON>(const std::string_view &text, Configuration& out) {
/*e.g.
{
  "ThemisConfiguration": {
    "CompilerConfigurations": {
      "Identifier": "THEMISCompiler",
      "Item": [
        {
          "ext": ".cpp",
          "cmd": "\"D:\\llvm-mingw-20251216-ucrt-x86_64\\bin\\g++.exe\" -std=c++14 \"%NAME%%EXT%\" -pipe -O2 -s -static -lm -x c++ -o\"%NAME%.exe\" -Wl,--stack,66060288 -Werror=pedantic -Werror=vla|@WorkDir=%PATH%"
        },
        {
          "ext": ".c",
          "cmd": "\"D:\\llvm-mingw-20251216-ucrt-x86_64\\bin\\gcc.exe\" -std=c11 \"%NAME%%EXT%\" -pipe -O2 -s -static -lm -x c -o\"%NAME%.exe\" -Wl,--stack,66060288 -Werror=pedantic -Werror=vla|@WorkDir=%PATH%"
        },
        {
          "ext": ".pas",
          "cmd": "\"C:\\Program Files (x86)\\Themis\\fpc\\bin\\i386-win32\\fpc.exe\" -o\"%NAME%.exe\" -O2 -XS -Sg -Cs66060288 \"%NAME%%EXT%\"|@WorkDir=%PATH%"
        },
        {
          "ext": ".pp",
          "cmd": "\"C:\\Program Files (x86)\\Themis\\fpc\\bin\\i386-win32\\fpc.exe\" -o\"%NAME%.exe\" -O2 -XS -Sg -Cs66060288 \"%NAME%%EXT%\"|@WorkDir=%PATH%"
        },
        {
          "ext": ".java",
          "cmd": "\"E:\\Program Files\\Microsoft\\jdk-25.0.1.8-hotspot\\bin\\javac.exe\" \"%NAME%%EXT%\"|@WorkDir=%PATH%"
        },
        {
          "ext": ".exe",
          "cmd": ";Nếu không muốn dịch lại khi đã có file .exe, chuyển loại file này lên đầu"
        },
        {
          "ext": ".class",
          "cmd": ";Nếu không muốn dịch lại khi đã có file .class, chuyển loại file này lên đầu"
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
      "LastExamDir": "C:\\Users\\winapiadmin\\Downloads\\Compressed\\New folder\\tests",
      "LastContestantDir": "H:\\De Hai phong 2025",
      "ExamEditAction": 0,
      "ToolBarVisible": true
    }
  }
}
*/
    nlohmann::json j = nlohmann::json::parse(text);

    // CompilerConfigurations
    if (j.contains("CompilerConfigurations")) {
        const auto& cc = j["CompilerConfigurations"];

        out.compiler.identifier =
            cc.value("Identifier", "");

        out.compiler.items.clear();

        if (cc.contains("Item") && cc["Item"].is_array()) {
            for (const auto& item : cc["Item"]) {
                CompilerItem ci;
                ci.ext = item.value("ext", "");
                ci.cmd = item.value("cmd", "");

                out.compiler.items.push_back(std::move(ci));
            }
        }
    }

    // Environment
    if (j.contains("Environment")) {
        const auto& env = j["Environment"];
        auto& e = out.environment;

        e.identifier = env.value("Identifier", "");
        e.submitDir = env.value("SubmitDir", "");
        e.decompressDir = env.value("DecompressDir", "");
        e.contestHouse = env.value("ContestHouse", "");
        e.adminUserName = env.value("AdminUserName", "");
        e.adminPassword = env.value("AdminPassword", "");
        e.adminDomain = env.value("AdminDomain", "");
        e.lastExamDir = env.value("LastExamDir", "");
        e.lastContestantDir = env.value("LastContestantDir", "");

        e.activeSecurity = env.value("ActiveSecurity", false);
        e.examEditAction = env.value("ExamEditAction", 0);
        e.toolBarVisible = env.value("ToolBarVisible", true);
    }
}
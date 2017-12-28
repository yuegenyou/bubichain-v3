/*
Copyright Bubi Technologies Co., Ltd. 2017 All Rights Reserved.
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <utils/logger.h>
#include <common/pb2json.h>
#include "ledger_frm.h"
#include "ledger_manager.h"
#include "contract_manager.h"

    
namespace bubi{

	ContractParameter::ContractParameter() : ope_index_(-1), ledger_context_(NULL){}

	ContractParameter::~ContractParameter() {}

	ContractTestParameter::ContractTestParameter() : exe_or_query_(true) {}

	ContractTestParameter::~ContractTestParameter() {}

	utils::Mutex Contract::contract_id_seed_lock_;
	int64_t Contract::contract_id_seed_ = 0; 
	Contract::Contract() {
		utils::MutexGuard guard(contract_id_seed_lock_);
		id_ = contract_id_seed_;
		contract_id_seed_++;
		tx_do_count_ = 0;
		readonly_ = false;
	}

	Contract::Contract(bool readonly, const ContractParameter &parameter) :
		readonly_(readonly), parameter_(parameter) {
		utils::MutexGuard guard(contract_id_seed_lock_);
		id_ = contract_id_seed_;
		contract_id_seed_++;
	}

	Contract::~Contract() {}

	bool Contract::Execute() {
		return true;
	}

	bool Contract::Cancel() {
		return true;
	}

	bool Contract::Query(Json::Value& jsResult) {
		return true;
	}

	bool Contract::SourceCodeCheck() {
		return true;
	}

	int64_t Contract::GetId() {
		return id_;
	}

	int32_t Contract::GetTxDoCount() {
		return tx_do_count_;
	}

	void Contract::IncTxDoCount() {
		tx_do_count_++;
	}

	const ContractParameter &Contract::GetParameter() {
		return parameter_;
	}

	bool Contract::IsReadonly() {
		return readonly_;
	}

	const utils::StringList &Contract::GetLogs() {
		return logs_;
	}

	void Contract::AddLog(const std::string &log) {
		logs_.push_back(log);
		if (logs_.size() > 100) logs_.pop_front();
	}

	std::string Contract::GetErrorMsg() {
		return error_msg_;
	}

	std::map<std::string, std::string> V8Contract::jslib_sources;
	const std::string V8Contract::sender_name_ = "sender";
	const std::string V8Contract::this_address_ = "thisAddress";
	const char* V8Contract::main_name_ = "main";
	const char* V8Contract::query_name_ = "query";
	const std::string V8Contract::trigger_tx_name_ = "trigger";
	const std::string V8Contract::trigger_tx_index_name_ = "triggerIndex";
	const std::string V8Contract::this_header_name_ = "consensusValue";
	utils::Mutex V8Contract::isolate_to_contract_mutex_;
	std::unordered_map<v8::Isolate*, V8Contract *> V8Contract::isolate_to_contract_;

	v8::Platform* V8Contract::platform_ = nullptr;
	v8::Isolate::CreateParams V8Contract::create_params_;

	V8Contract::V8Contract(bool readonly, const ContractParameter &parameter) : Contract(readonly,parameter) {
		type_ = TYPE_V8;
		isolate_ = v8::Isolate::New(create_params_);

		utils::MutexGuard guard(isolate_to_contract_mutex_);
		isolate_to_contract_[isolate_] = this;
	}

	V8Contract::~V8Contract() {
		utils::MutexGuard guard(isolate_to_contract_mutex_);
		isolate_to_contract_.erase(isolate_);
		isolate_->Dispose();
		isolate_ = NULL;
	}

	bool V8Contract::LoadJsLibSource() {
		std::string lib_path = utils::String::Format("%s/jslib", utils::File::GetBinHome().c_str());
		utils::FileAttributes files;
		utils::File::GetFileList(lib_path, "*.js", files);
		for (utils::FileAttributes::iterator iter = files.begin(); iter != files.end(); iter++) {
			utils::FileAttribute attr = iter->second;
			utils::File file;
			std::string file_path = utils::String::Format("%s/%s", lib_path.c_str(), iter->first.c_str());
			if (!file.Open(file_path, utils::File::FILE_M_READ)) {
				LOG_ERROR_ERRNO("Open js lib file failed, path(%s)", file_path.c_str(), STD_ERR_CODE, STD_ERR_DESC);
				continue;
			}

			std::string data;
			if (file.ReadData(data, 10 * utils::BYTES_PER_MEGA) < 0) {
				LOG_ERROR_ERRNO("Read js lib file failed, path(%s)", file_path.c_str(), STD_ERR_CODE, STD_ERR_DESC);
				continue;
			}

			jslib_sources[iter->first] = data;
		}

		return true;
	}

	bool V8Contract::Initialize(int argc, char** argv) {
		LoadJsLibSource();
		v8::V8::InitializeICUDefaultLocation(argv[0]);
		v8::V8::InitializeExternalStartupData(argv[0]);
		platform_ = v8::platform::CreateDefaultPlatform();
		v8::V8::InitializePlatform(platform_);
		if (!v8::V8::Initialize()) {
			LOG_ERROR("V8 Initialize failed");
			return false;
		}
		create_params_.array_buffer_allocator =
			v8::ArrayBuffer::Allocator::NewDefaultAllocator();

		return true;
	}

	bool V8Contract::Execute() {
		v8::Isolate::Scope isolate_scope(isolate_);
		v8::HandleScope handle_scope(isolate_);
		v8::TryCatch try_catch(isolate_);

		v8::Local<v8::Context> context = CreateContext(isolate_, false);

		v8::Context::Scope context_scope(context);

		v8::Local<v8::Value> vtoken = v8::String::NewFromUtf8(isolate_, parameter_.this_address_.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
		context->SetSecurityToken(vtoken);

		auto string_sender = v8::String::NewFromUtf8(isolate_, parameter_.sender_.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
		context->Global()->Set(context,
			v8::String::NewFromUtf8(isolate_, sender_name_.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
			string_sender);

		auto string_contractor = v8::String::NewFromUtf8(isolate_, parameter_.this_address_.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
		context->Global()->Set(context,
			v8::String::NewFromUtf8(isolate_, this_address_.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
			string_contractor);


		auto str_json_v8 = v8::String::NewFromUtf8(isolate_, parameter_.trigger_tx_.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
		auto tx_v8 = v8::JSON::Parse(str_json_v8);
		context->Global()->Set(context,
			v8::String::NewFromUtf8(isolate_, trigger_tx_name_.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
			tx_v8);

		v8::Local<v8::Integer> index_v8 = v8::Int32::New(isolate_, parameter_.ope_index_);
		context->Global()->Set(context,
			v8::String::NewFromUtf8(isolate_, trigger_tx_index_name_.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
			index_v8);

		auto v8_consensus_value = v8::String::NewFromUtf8(isolate_, parameter_.consensus_value_.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
		auto v8HeadJson = v8::JSON::Parse(v8_consensus_value);
		context->Global()->Set(context,
			v8::String::NewFromUtf8(isolate_, this_header_name_.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
			v8HeadJson);

		v8::Local<v8::String> v8src = v8::String::NewFromUtf8(isolate_, parameter_.code_.c_str());
		v8::Local<v8::Script> compiled_script;

		do {
			Json::Value error_random;
			if (!RemoveRandom(isolate_, error_random)) {
				error_msg_ = error_random.toFastString();
				break;
			}
		
			if (!v8::Script::Compile(context, v8src).ToLocal(&compiled_script)) {
				error_msg_ = ReportException(isolate_, &try_catch).toFastString();
				break;
			}

			v8::Local<v8::Value> result;
			if (!compiled_script->Run(context).ToLocal(&result)) {
				error_msg_ = ReportException(isolate_, &try_catch).toFastString();
				break;
			}

			v8::Local<v8::String> process_name =
				v8::String::NewFromUtf8(isolate_, main_name_, v8::NewStringType::kNormal, strlen(main_name_))
				.ToLocalChecked();
			v8::Local<v8::Value> process_val;

			if (!context->Global()->Get(context, process_name).ToLocal(&process_val) ||
				!process_val->IsFunction()) {
				LOG_ERROR("lost of %s function", main_name_);
				break;
			}

			v8::Local<v8::Function> process = v8::Local<v8::Function>::Cast(process_val);

			const int argc = 1;
			v8::Local<v8::String> arg1 = v8::String::NewFromUtf8(isolate_, parameter_.input_.c_str(), v8::NewStringType::kNormal).ToLocalChecked();

			v8::Local<v8::Value> argv[argc];
			argv[0] = arg1;

			v8::Local<v8::Value> callresult;
			if (!process->Call(context, context->Global(), argc, argv).ToLocal(&callresult)) {
				error_msg_ = ReportException(isolate_, &try_catch).toFastString();
				break;
			}

			return true;
		} while (false);
		return false;
	}

	bool V8Contract::Cancel() {
		v8::V8::TerminateExecution(isolate_);
		return true;
	}

	bool V8Contract::SourceCodeCheck() {

		v8::Isolate::Scope isolate_scope(isolate_);
		v8::HandleScope handle_scope(isolate_);
		v8::TryCatch try_catch(isolate_);

		v8::Local<v8::Context> context = CreateContext(isolate_, false);
		v8::Context::Scope context_scope(context);


		auto string_sender = v8::String::NewFromUtf8(isolate_, "", v8::NewStringType::kNormal).ToLocalChecked();
		context->Global()->Set(context, v8::String::NewFromUtf8(isolate_, sender_name_.c_str(), v8::NewStringType::kNormal).ToLocalChecked(), string_sender);


		auto string_contractor = v8::String::NewFromUtf8(isolate_, "", v8::NewStringType::kNormal).ToLocalChecked();
		context->Global()->Set(context, v8::String::NewFromUtf8(isolate_, this_address_.c_str(), v8::NewStringType::kNormal).ToLocalChecked(), string_contractor);

		auto str_json_v8 = v8::String::NewFromUtf8(isolate_, "{}", v8::NewStringType::kNormal).ToLocalChecked();
		auto tx_v8 = v8::JSON::Parse(str_json_v8);
		context->Global()->Set(context,
			v8::String::NewFromUtf8(isolate_, trigger_tx_name_.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
			tx_v8);

		v8::Local<v8::Integer> index_v8 = v8::Int32::New(isolate_, 0);
		context->Global()->Set(context,
			v8::String::NewFromUtf8(isolate_, trigger_tx_index_name_.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
			index_v8);

		auto v8_consensus_value = v8::String::NewFromUtf8(isolate_, "{}", v8::NewStringType::kNormal).ToLocalChecked();
		auto v8HeadJson = v8::JSON::Parse(v8_consensus_value);
		context->Global()->Set(context,
			v8::String::NewFromUtf8(isolate_, this_header_name_.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
			v8HeadJson);

		v8::Local<v8::String> v8src = v8::String::NewFromUtf8(isolate_, parameter_.code_.c_str());
		v8::Local<v8::Script> compiled_script;

		if (!v8::Script::Compile(context, v8src).ToLocal(&compiled_script)) {
			error_msg_ = ReportException(isolate_, &try_catch).toFastString();
			LOG_ERROR("%s", error_msg_.c_str());
			return false;
		}

		/*
		auto result = compiled_script->Run(context).ToLocalChecked();

		v8::Local<v8::String> process_name =
		v8::String::NewFromUtf8(isolate_, main_name_
		, v8::NewStringType::kNormal, strlen(main_name_))
		.ToLocalChecked();


		v8::Local<v8::Value> process_val;

		if (!context->Global()->Get(context, process_name).ToLocal(&process_val) ) {
		err_msg = utils::String::Format("lost of %s function", main_name_);
		LOG_ERROR("%s", err_msg.c_str());
		return false;
		}

		if (!process_val->IsFunction()){
		err_msg = utils::String::Format("lost of %s function", main_name_);
		LOG_ERROR("%s", err_msg.c_str());
		return false;
		}
		*/
		return true;
	}

	bool V8Contract::Query(Json::Value& js_result) {
		v8::Isolate::Scope isolate_scope(isolate_);
		v8::HandleScope    handle_scope(isolate_);
		v8::TryCatch       try_catch(isolate_);

		v8::Local<v8::Context>       context = CreateContext(isolate_, true);
		v8::Context::Scope            context_scope(context);

		v8::Local<v8::Value> vtoken = v8::String::NewFromUtf8(isolate_, parameter_.this_address_.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
		context->SetSecurityToken(vtoken);

		auto string_sender = v8::String::NewFromUtf8(isolate_, parameter_.sender_.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
		context->Global()->Set(context,
			v8::String::NewFromUtf8(isolate_, sender_name_.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
			string_sender);

		auto string_contractor = v8::String::NewFromUtf8(isolate_, parameter_.this_address_.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
		context->Global()->Set(context,
			v8::String::NewFromUtf8(isolate_, this_address_.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
			string_contractor);


		auto str_json_v8 = v8::String::NewFromUtf8(isolate_, parameter_.trigger_tx_.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
		auto tx_v8 = v8::JSON::Parse(str_json_v8);
		context->Global()->Set(context,
			v8::String::NewFromUtf8(isolate_, trigger_tx_name_.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
			tx_v8);

		v8::Local<v8::Integer> index_v8 = v8::Int32::New(isolate_, parameter_.ope_index_);
		context->Global()->Set(context,
			v8::String::NewFromUtf8(isolate_, trigger_tx_index_name_.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
			index_v8);

		auto v8_consensus_value = v8::String::NewFromUtf8(isolate_, parameter_.consensus_value_.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
		auto v8HeadJson = v8::JSON::Parse(v8_consensus_value);
		context->Global()->Set(context,
			v8::String::NewFromUtf8(isolate_, this_header_name_.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
			v8HeadJson);


		v8::Local<v8::String> v8src = v8::String::NewFromUtf8(isolate_, parameter_.code_.c_str());
		v8::Local<v8::Script> compiled_script;

		Json::Value error_desc_f;
		Json::Value temp_result;
		do {
			if (!RemoveRandom(isolate_, error_desc_f)) {
				break;
			}

			if (!v8::Script::Compile(context, v8src).ToLocal(&compiled_script)) {
				error_desc_f = ReportException(isolate_, &try_catch);
				break;
			}

			v8::Local<v8::Value> result;
			if (!compiled_script->Run(context).ToLocal(&result)) {
				error_desc_f = ReportException(isolate_, &try_catch);
				break;
			}

			v8::Local<v8::String> process_name = v8::String::NewFromUtf8(
				isolate_, query_name_, v8::NewStringType::kNormal, strlen(query_name_)).ToLocalChecked();

			v8::Local<v8::Value> process_val;
			if (!context->Global()->Get(context, process_name).ToLocal(&process_val) ||
				!process_val->IsFunction()) {
				Json::Value &exception = error_desc_f["exception"];
				exception = utils::String::Format("Lost of %s function", query_name_);
				LOG_ERROR("%s", exception.asCString());
				break;
			}

			v8::Local<v8::Function> process = v8::Local<v8::Function>::Cast(process_val);

			const int argc = 1;
			v8::Local<v8::Value>  argv[argc];
			v8::Local<v8::String> arg1 = v8::String::NewFromUtf8(isolate_, parameter_.input_.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
			argv[0] = arg1;

			v8::Local<v8::Value> callRet;
			if (!process->Call(context, context->Global(), argc, argv).ToLocal(&callRet)) {
				error_desc_f = ReportException(isolate_, &try_catch);
				LOG_ERROR("%s function execute failed", query_name_);
				break;
			}

			JsValueToCppJson(context, callRet, temp_result);
			Json::Value &result_v = js_result["result"];
			result_v[result_v.size()] = temp_result;
			return true;
		} while (false);

		js_result["error_desc_f"] = error_desc_f;
		return false;
	}


	V8Contract *V8Contract::GetContractFrom(v8::Isolate* isolate) {
		utils::MutexGuard guard(isolate_to_contract_mutex_);
		std::unordered_map<v8::Isolate*, V8Contract *>::iterator iter = isolate_to_contract_.find(isolate);
		if (iter != isolate_to_contract_.end()){
			return iter->second;
		}

		return NULL;
	}

	bool V8Contract::RemoveRandom(v8::Isolate* isolate, Json::Value &error_msg) {
		v8::TryCatch try_catch(isolate);
		std::string js_file = "delete Date; delete Math.random;";

		v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, js_file.c_str());
		v8::Local<v8::Script> script;
		if (!v8::Script::Compile(isolate->GetCurrentContext(), source).ToLocal(&script)) {
			error_msg = ReportException(isolate, &try_catch);
			return false;
		}

		v8::Local<v8::Value> result;
		if (!script->Run(isolate->GetCurrentContext()).ToLocal(&result)) {
			error_msg = ReportException(isolate, &try_catch);
			return false;
		}

		return true;
	}

	v8::Local<v8::Context> V8Contract::CreateContext(v8::Isolate* isolate, bool readonly) {
		// Create a template for the global object.
		v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);
		// Bind the global 'print' function to the C++ Print callback.
		global->Set(
			v8::String::NewFromUtf8(isolate, "callBackLog", v8::NewStringType::kNormal)
			.ToLocalChecked(),
			v8::FunctionTemplate::New(isolate, V8Contract::CallBackLog));
		
		global->Set(
			v8::String::NewFromUtf8(isolate, "callBackGetAccountInfo", v8::NewStringType::kNormal)
			.ToLocalChecked(),
			v8::FunctionTemplate::New(isolate, V8Contract::CallBackGetAccountInfo));
		
		
		global->Set(
			v8::String::NewFromUtf8(isolate, "callBackGetAccountAsset", v8::NewStringType::kNormal)
			.ToLocalChecked(),
			v8::FunctionTemplate::New(isolate, V8Contract::CallBackGetAccountAsset));
		
		global->Set(
			v8::String::NewFromUtf8(isolate, "callBackGetAccountMetaData", v8::NewStringType::kNormal)
			.ToLocalChecked(),
			v8::FunctionTemplate::New(isolate, V8Contract::CallBackGetAccountMetaData));

		global->Set(
			v8::String::NewFromUtf8(isolate, "callBackContractQuery", v8::NewStringType::kNormal)
			.ToLocalChecked(),
			v8::FunctionTemplate::New(isolate, V8Contract::CallBackContractQuery));
		
		if (!readonly) {
			global->Set(
				v8::String::NewFromUtf8(isolate, "callBackSetAccountMetaData", v8::NewStringType::kNormal)
				.ToLocalChecked(),
				v8::FunctionTemplate::New(isolate, V8Contract::CallBackSetAccountMetaData));

			global->Set(
				v8::String::NewFromUtf8(isolate, "callBackDoOperation", v8::NewStringType::kNormal)
				.ToLocalChecked(),
				v8::FunctionTemplate::New(isolate, V8Contract::CallBackDoOperation));
		} 
		
		global->Set(
			v8::String::NewFromUtf8(isolate, "callBackGetLedgerInfo", v8::NewStringType::kNormal)
			.ToLocalChecked(),
			v8::FunctionTemplate::New(isolate, V8Contract::CallBackGetLedgerInfo));
		
		/*		global->Set(
		v8::String::NewFromUtf8(isolate_, "callBackGetTransactionInfo", v8::NewStringType::kNormal)
		.ToLocalChecked(),
		v8::FunctionTemplate::New(isolate_, ContractManager::CallBackGetTransactionInfo, v8::External::New(isolate_, this)));*/

		
		global->Set(
			v8::String::NewFromUtf8(isolate, "include", v8::NewStringType::kNormal)
			.ToLocalChecked(),
			v8::FunctionTemplate::New(isolate, V8Contract::Include));

		return v8::Context::New(isolate, NULL, global);
	}

	Json::Value V8Contract::ReportException(v8::Isolate* isolate, v8::TryCatch* try_catch) {
		v8::HandleScope handle_scope(isolate);
		v8::String::Utf8Value exception(try_catch->Exception());
		const char* exception_string = ToCString(exception);
		std::string exec_string(exception_string);
		exec_string.resize(256);
		Json::Value json_result;

		v8::Local<v8::Message> message = try_catch->Message();
		std::string error_msg;
		if (message.IsEmpty()) {
			// V8 didn't provide any extra information about this error; just
			// print the exception.
			json_result["exception"] = exec_string;
		}
		else {
			// Print (filename):(line number): (message).
			v8::String::Utf8Value filename(message->GetScriptOrigin().ResourceName());
			v8::Local<v8::Context> context(isolate->GetCurrentContext());
			const char* filename_string = ToCString(filename);
			int linenum = message->GetLineNumber(context).FromJust();
			json_result["filename"] = filename_string;
			json_result["linenum"] = linenum;
			json_result["exception"] = exec_string;

			//print error stack
			v8::Local<v8::Value> stack_trace_string;
			if (try_catch->StackTrace(context).ToLocal(&stack_trace_string) &&
				stack_trace_string->IsString() &&
				v8::Local<v8::String>::Cast(stack_trace_string)->Length() > 0) {
				v8::String::Utf8Value stack_trace(stack_trace_string);
				const char* stack_trace_string = ToCString(stack_trace);
				json_result["stack"] = stack_trace_string;
			}
		}
		return json_result;
	}

	bool V8Contract::CppJsonToJsValue(v8::Isolate* isolate, Json::Value& jsonvalue, v8::Local<v8::Value>& jsvalue) {
		std::string type = jsonvalue["type"].asString();
		if (type == "jsobject") {
			std::string value = jsonvalue["value"].asString();
			v8::Local<v8::String> str = v8::String::NewFromUtf8(isolate, value.c_str());
			jsvalue = v8::JSON::Parse(str);
		}
		else if (type == "number") {
			std::string value = jsonvalue["value"].asString();
			std::string bin_double = utils::String::HexStringToBin(value);
			double d_value = 0;
			memcpy(&d_value, bin_double.c_str(), sizeof(double));
			jsvalue = v8::Number::New(isolate, d_value);
		}
		else if (type == "string") {
			jsvalue = v8::String::NewFromUtf8(isolate, jsonvalue["value"].asCString());
		}
		else if (type == "bool") {
			jsvalue = v8::Boolean::New(isolate, jsonvalue["value"].asBool());
		}

		return true;
	}

	bool V8Contract::JsValueToCppJson(v8::Handle<v8::Context>& context, v8::Local<v8::Value>& jsvalue, Json::Value& jsonvalue) {
		if (jsvalue->IsObject()) {  //include map arrary
			v8::Local<v8::String> jsStr = v8::JSON::Stringify(context, jsvalue->ToObject()).ToLocalChecked();
			std::string str = std::string(ToCString(v8::String::Utf8Value(jsStr)));
			
			jsonvalue["type"] = "jsobject";
			jsonvalue["value"] = str;
		}
		else if (jsvalue->IsNumber()) {
			double s_value = jsvalue->NumberValue();
			std::string value;
			value.resize(sizeof(double));
			memcpy((void *)value.c_str(), &s_value, sizeof(double));
			jsonvalue["type"] = "number";
			jsonvalue["value"] = utils::String::BinToHexString(value);
			jsonvalue["valuePlain"] = jsvalue->NumberValue();
		}
		else if (jsvalue->IsBoolean()) {
			jsonvalue["type"] = "bool";
			jsonvalue["value"] = jsvalue->BooleanValue();
		}
		else if (jsvalue->IsString()) {
			jsonvalue["type"] = "string";
			jsonvalue["value"] = std::string(ToCString(v8::String::Utf8Value(jsvalue)));
		}
		else {
			jsonvalue["type"] = "bool";
			jsonvalue["value"] = false;
		}

		return true;
	}

	V8Contract* V8Contract::UnwrapContract(v8::Local<v8::Object> obj) {
		v8::Local<v8::External> field = v8::Local<v8::External>::Cast(obj->GetInternalField(0));
		void* ptr = field->Value();
		return static_cast<V8Contract*>(ptr);
	}

	const char* V8Contract::ToCString(const v8::String::Utf8Value& value) {
		return *value ? *value : "<string conversion failed>";
	}

	void V8Contract::Include(const v8::FunctionCallbackInfo<v8::Value>& args) {
		do {
			if (args.Length() != 1) {
				LOG_ERROR("Include parameter error, args length(%d) not equal 1", args.Length());
				args.GetReturnValue().Set(false);
				break;
			}

			if (!args[0]->IsString()) {
				LOG_ERROR("Include parameter error, parameter should be a String");
				args.GetReturnValue().Set(false);
				break;
			}
			v8::String::Utf8Value str(args[0]);

			std::map<std::string, std::string>::iterator find_source = jslib_sources.find(*str);
			if (find_source == jslib_sources.end()) {
				LOG_ERROR("Can't find the include file(%s) in jslib directory", *str);
				args.GetReturnValue().Set(false);
				break;
			}

			v8::TryCatch try_catch(args.GetIsolate());
			std::string js_file = find_source->second; //load_file(*str);

			v8::Local<v8::String> source = v8::String::NewFromUtf8(args.GetIsolate(), js_file.c_str());
			v8::Local<v8::Script> script;
			if (!v8::Script::Compile(args.GetIsolate()->GetCurrentContext(), source).ToLocal(&script)) {
				ReportException(args.GetIsolate(), &try_catch);
				break;
			}

			v8::Local<v8::Value> result;
			if (!script->Run(args.GetIsolate()->GetCurrentContext()).ToLocal(&result)) {
				ReportException(args.GetIsolate(), &try_catch);
			}
		} while (false);
		//return v8::Undefined(args.GetIsolate());
	}

	void V8Contract::CallBackLog(const v8::FunctionCallbackInfo<v8::Value>& args) {
		LOG_INFO("CallBackLog");

		if (args.Length() < 1) {
			args.GetReturnValue().Set(false);
			return;
		}
		v8::HandleScope scope(args.GetIsolate());

		v8::String::Utf8Value token(args.GetIsolate()->GetCurrentContext()->GetSecurityToken()->ToString());

		v8::Local<v8::String> str;
		if (args[0]->IsObject()) {
			v8::Local<v8::Object> obj = args[0]->ToObject(args.GetIsolate());
			str = v8::JSON::Stringify(args.GetIsolate()->GetCurrentContext(), obj).ToLocalChecked();
		}
		else {
			str = args[0]->ToString();
		}
		
		auto type = args[0]->TypeOf(args.GetIsolate());
		LOG_INFO("type is %s", ToCString(v8::String::Utf8Value(type)));
		if (v8::String::NewFromUtf8(args.GetIsolate(), "undefined", v8::NewStringType::kNormal).ToLocalChecked()->Equals(type)) {
			LOG_INFO("undefined type");
			return;
		}

		//
		auto context = args.GetIsolate()->GetCurrentContext();
		auto sender = args.GetIsolate()->GetCurrentContext()->Global()->Get(context,
			v8::String::NewFromUtf8(args.GetIsolate(), sender_name_.c_str(), v8::NewStringType::kNormal).ToLocalChecked()).ToLocalChecked();
		v8::String::Utf8Value utf8_sender(sender->ToString());
		//
		v8::String::Utf8Value utf8value(str);
		LOG_INFO("LogCallBack[%s:%s]\n%s", ToCString(token), ToCString(utf8_sender), ToCString(utf8value));

		V8Contract *v8_contract = GetContractFrom(args.GetIsolate());
		if (v8_contract) {
			v8_contract->AddLog(ToCString(utf8value));
		} 
	}

	void V8Contract::CallBackGetAccountAsset(const v8::FunctionCallbackInfo<v8::Value>& args) {
		if (args.Length() != 2) {
			LOG_ERROR("parameter error");
			args.GetReturnValue().Set(false);
			return;
		}

		do {
			v8::HandleScope handle_scope(args.GetIsolate());
			if (!args[0]->IsString()) {
				LOG_ERROR("contract execute error,CallBackGetAccountAsset, parameter 1 should be a String");
				break;
			}
			auto address = std::string(ToCString(v8::String::Utf8Value(args[0])));

			if (!args[1]->IsObject()) {
				LOG_ERROR("contract execute error,CallBackGetAccountAsset parameter 2 should be a object");
				break;
			}
			auto ss = v8::JSON::Stringify(args.GetIsolate()->GetCurrentContext(), args[1]->ToObject()).ToLocalChecked();
			auto strjson = std::string(ToCString(v8::String::Utf8Value(ss)));
			Json::Value json;
			json.fromString(strjson);

			protocol::AssetProperty property;
			std::string error_msg;
			if (!Json2Proto(json, property, error_msg)) {
				LOG_ERROR("contract execute error,CallBackGetAccountAsset,parameter property not valid. error=%s", error_msg.c_str());
				break;
			}

			bubi::AccountFrm::pointer account_frm = nullptr;
			V8Contract *v8_contract = GetContractFrom(args.GetIsolate());

			bool getAccountSucceed = false;
			if (v8_contract && v8_contract->GetParameter().ledger_context_) {
				LedgerContext *ledger_context = v8_contract->GetParameter().ledger_context_;
				if (!ledger_context->transaction_stack_.empty()) {
					auto environment = ledger_context->transaction_stack_.top()->environment_;
					if (!environment->GetEntry(address, account_frm)) {
						LOG_ERROR("not found account");
						break;
					}
					else {
						getAccountSucceed = true;
					}
				}
			}

			if (!getAccountSucceed) {
				if (!Environment::AccountFromDB(address, account_frm)) {
					LOG_ERROR("not found account");
					break;
				}
			}

			protocol::Asset asset;
			if (!account_frm->GetAsset(property, asset)) {
				break;
			}

			Json::Value json_asset = bubi::Proto2Json(asset);
			std::string strvalue = json_asset.toFastString();

			v8::Local<v8::String> returnvalue = v8::String::NewFromUtf8(
				args.GetIsolate(), strvalue.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
			args.GetReturnValue().Set(v8::JSON::Parse(returnvalue));

		} while (false);
	}

	void V8Contract::CallBackGetAccountMetaData(const v8::FunctionCallbackInfo<v8::Value>& args) {
		do {
			if (args.Length() != 2) {
				LOG_ERROR("parameter error");
				args.GetReturnValue().Set(false);
				break;
			}
			v8::HandleScope handle_scope(args.GetIsolate());

			if (!args[0]->IsString()) {
				LOG_ERROR("contract execute error,CallBackGetAccountStorage, parameter 0 should be a String");
				break;
			}

			v8::String::Utf8Value str(args[0]);
			std::string address(ToCString(str));

			if (!args[1]->IsString()) {
				LOG_ERROR("contract execute error,CallBackGetAccountStorage, parameter 1 should be a String");
				break;
			}
			std::string key = ToCString(v8::String::Utf8Value(args[1]));

			bubi::AccountFrm::pointer account_frm = nullptr;
			V8Contract *v8_contract = GetContractFrom(args.GetIsolate());

			bool getAccountSucceed = false;
			if (v8_contract && v8_contract->GetParameter().ledger_context_) {
				LedgerContext *ledger_context = v8_contract->GetParameter().ledger_context_;
				if (!ledger_context->transaction_stack_.empty()) {
					auto environment = ledger_context->transaction_stack_.top()->environment_;
					if (!environment->GetEntry(address, account_frm)) {
						LOG_ERROR("not found account");
						break;
					}
					else {
						getAccountSucceed = true;
					}
				}
			}

			if (!getAccountSucceed) {
				if (!Environment::AccountFromDB(address, account_frm)) {
					LOG_ERROR("not found account");
					break;
				}
			}

			protocol::KeyPair kp;
			if (!account_frm->GetMetaData(key, kp)) {
				break;
			}

			Json::Value json = bubi::Proto2Json(kp);
			std::string strvalue = json.toFastString();

			v8::Local<v8::String> returnvalue = v8::String::NewFromUtf8(
				args.GetIsolate(), strvalue.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
			args.GetReturnValue().Set(v8::JSON::Parse(returnvalue));
			return;
		} while (false);
		args.GetReturnValue().Set(false);
	}


	void V8Contract::CallBackSetAccountMetaData(const v8::FunctionCallbackInfo<v8::Value>& args) {
		do {
			if (args.Length() != 1) {
				LOG_ERROR("parameter error");
				args.GetReturnValue().Set(false);
				break;
			}
			v8::HandleScope handle_scope(args.GetIsolate());

			v8::String::Utf8Value token(args.GetIsolate()->GetCurrentContext()->GetSecurityToken()->ToString());
			std::string contractor(ToCString(token));

			if (!args[0]->IsObject()) {
				LOG_ERROR("contract execute error,CallBackSetAccountStorage, parameter 0 should be a object");
				break;
			}
			v8::Local<v8::String> str = v8::JSON::Stringify(args.GetIsolate()->GetCurrentContext(), args[0]->ToObject()).ToLocalChecked();
			v8::String::Utf8Value  utf8(str);

			protocol::TransactionEnv txenv;
			txenv.mutable_transaction()->set_source_address(contractor);
			protocol::Operation *ope = txenv.mutable_transaction()->add_operations();

			Json::Value json;
			if (!json.fromCString(ToCString(utf8))) {
				LOG_ERROR("fromCString fail, fatal error");
				break;
			}

			ope->set_type(protocol::Operation_Type_SET_METADATA);
			protocol::OperationSetMetadata proto_setmetadata;
			std::string error_msg;
			if (!Json2Proto(json, proto_setmetadata, error_msg)) {
				LOG_ERROR("json error=%s", error_msg.c_str());
				break;
			}
			ope->mutable_set_metadata()->CopyFrom(proto_setmetadata);

			V8Contract *v8_contract = GetContractFrom(args.GetIsolate());
			if (!v8_contract || !v8_contract->parameter_.ledger_context_) {
				LOG_ERROR("Can't find contract object by isolate id");
				break;
			}

			if (v8_contract->IsReadonly()) {
				LOG_ERROR("The contract is readonly");
				break;
			}

			if (!LedgerManager::Instance().DoTransaction(txenv, v8_contract->parameter_.ledger_context_)) {
				LOG_ERROR("Do transaction failed");
				break;
			}
		
			args.GetReturnValue().Set(true);
			return;
		} while (false);
		args.GetReturnValue().Set(false);
	}

	//
	void V8Contract::CallBackGetAccountInfo(const v8::FunctionCallbackInfo<v8::Value>& args) {
		do {
			if (args.Length() != 1) {
				LOG_ERROR("parameter error");
				args.GetReturnValue().Set(false);
				break;
			}

			v8::HandleScope handle_scope(args.GetIsolate());
			if (!args[0]->IsString()) {
				LOG_ERROR("CallBackGetAccountInfo, parameter 0 should be a String");
				break;
			}

			v8::String::Utf8Value str(args[0]);
			std::string address(ToCString(str));

			bubi::AccountFrm::pointer account_frm = nullptr;
			V8Contract *v8_contract = GetContractFrom(args.GetIsolate());

			bool getAccountSucceed = false;
			if (v8_contract && v8_contract->GetParameter().ledger_context_) {
				LedgerContext *ledger_context = v8_contract->GetParameter().ledger_context_;
				if (!ledger_context->transaction_stack_.empty()) {
					auto environment = ledger_context->transaction_stack_.top()->environment_;
					if (!environment->GetEntry(address, account_frm)) {
						LOG_ERROR("not found account");
						break;
					}
					else {
						getAccountSucceed = true;
					}
				}
			}

			if (!getAccountSucceed) {
				if (!Environment::AccountFromDB(address, account_frm)) {
					LOG_ERROR("not found account");
					break;
				}
			}

			Json::Value json = bubi::Proto2Json(account_frm->GetProtoAccount());
			v8::Local<v8::String> returnvalue = v8::String::NewFromUtf8(
				args.GetIsolate(), json.toFastString().c_str(), v8::NewStringType::kNormal).ToLocalChecked();
			args.GetReturnValue().Set(v8::JSON::Parse(returnvalue));

			return;
		} while (false);
		args.GetReturnValue().Set(false);
	}

	void V8Contract::CallBackGetLedgerInfo(const v8::FunctionCallbackInfo<v8::Value>& args) {
		if (args.Length() != 1) {
			LOG_ERROR("parameter error");
			args.GetReturnValue().Set(false);
			return;
		}

		v8::HandleScope handle_scope(args.GetIsolate());
		v8::String::Utf8Value str(args[0]);
		std::string key(ToCString(str));

		int64_t seq = utils::String::Stoi64(key);
		protocol::LedgerHeader lcl = LedgerManager::Instance().GetLastClosedLedger();
		if (seq <= lcl.seq() - 1024 || seq > lcl.seq()) {
			args.GetReturnValue().Set(false);
			LOG_ERROR("The parameter seq(" FMT_I64 ") <= " FMT_I64 " or > " FMT_I64, seq, lcl.seq() - 1024, lcl.seq());
			return;
		}

		LedgerFrm lfrm;
		if (lfrm.LoadFromDb(seq)) {

			std::string strvalue = bubi::Proto2Json(lfrm.GetProtoHeader()).toStyledString();
			v8::Local<v8::String> returnvalue = v8::String::NewFromUtf8(
				args.GetIsolate(), strvalue.c_str(), v8::NewStringType::kNormal).ToLocalChecked();

			args.GetReturnValue().Set(v8::JSON::Parse(returnvalue));
		}
		else {
			args.GetReturnValue().Set(false);
		}
	}

	void V8Contract::CallBackContractQuery(const v8::FunctionCallbackInfo<v8::Value>& args) {

		v8::HandleScope handle_scope(args.GetIsolate());
		v8::Local<v8::Object> obj = v8::Object::New(args.GetIsolate());
		v8::Local<v8::Boolean> flag_false = v8::Boolean::New(args.GetIsolate(), false);
		obj->Set(v8::String::NewFromUtf8(args.GetIsolate(), "success"), flag_false);

		do {
			if (args.Length() != 2) {
				LOG_ERROR("parameter error");
				args.GetReturnValue().Set(false);
				break;
			}

			if (!args[0]->IsString()) { //the called contract address
				LOG_ERROR("contract execute error,CallBackContractQuery, parameter 0 should be a String");
				break;
			}

			if (!args[1]->IsString()) {
				LOG_ERROR("contract execute error,CallBackContractQuery, parameter 1 should be a String");
				break;
			}

			std::string address = ToCString((v8::String::Utf8Value)args[0]);
			std::string input = ToCString((v8::String::Utf8Value)args[1]);

			bubi::AccountFrm::pointer account_frm = nullptr;
			V8Contract *v8_contract = GetContractFrom(args.GetIsolate());

			bool getAccountSucceed = false;
			if (v8_contract && v8_contract->GetParameter().ledger_context_) {
				LedgerContext *ledger_context = v8_contract->GetParameter().ledger_context_;
				if (!ledger_context->transaction_stack_.empty()) {
					auto environment = ledger_context->transaction_stack_.top()->environment_;
					if (!environment->GetEntry(address, account_frm)) {
						LOG_ERROR("not found account");
						break;
					}
					else {
						getAccountSucceed = true;
					}
				}
			}
			else {
				LOG_ERROR("Server internal error");
				break;
			}

			if (!getAccountSucceed) {
				if (!Environment::AccountFromDB(address, account_frm)) {
					LOG_ERROR("not found account");
					break;
				}
			}

			if (!account_frm->GetProtoAccount().has_contract()) {
				LOG_ERROR("the called address not contract");
				break;
			}

			protocol::Contract contract = account_frm->GetProtoAccount().contract();
			if (contract.payload().size() == 0) {
				LOG_ERROR("the called address not contract");
				break;
			}

			ContractParameter parameter;
			parameter.code_ = contract.payload();
			parameter.sender_ = v8_contract->GetParameter().this_address_;
			parameter.this_address_ = address;
			parameter.input_ = input;
			parameter.ope_index_ = 0;
			parameter.trigger_tx_ = "{}";
			parameter.consensus_value_ = v8_contract->GetParameter().consensus_value_;
			parameter.ledger_context_ = v8_contract->GetParameter().ledger_context_;
			//do query

			Json::Value query_result;
			bool ret = ContractManager::Instance().Query(contract.type(), parameter, query_result);

			v8::Local<v8::Boolean> flag = v8::Boolean::New(args.GetIsolate(), ret);
			obj->Set(v8::String::NewFromUtf8(args.GetIsolate(), "success"), flag);

			Json::Value js_array = query_result["result"];
			if (ret && js_array.size() > 0) {

				v8::Local<v8::Value> v8_result;
				CppJsonToJsValue(args.GetIsolate(), js_array[(uint32_t)0], v8_result);
				obj->Set(v8::String::NewFromUtf8(args.GetIsolate(), "result"), v8_result);
			} 

		} while (false);

		args.GetReturnValue().Set(obj);
	}

	void V8Contract::CallBackDoOperation(const v8::FunctionCallbackInfo<v8::Value>& args) {

		do {
			if (args.Length() != 1) {
				args.GetReturnValue().SetNull();
				LOG_ERROR("parameter error");
				break;
			}
			v8::HandleScope handle_scope(args.GetIsolate());

			v8::String::Utf8Value token(args.GetIsolate()->GetCurrentContext()->GetSecurityToken()->ToString());
			std::string contractor(ToCString(token));

			v8::Local<v8::Object> obj = args[0]->ToObject();
			if (obj->IsNull()) {
				LOG_ERROR("CallBackDoOperation, parameter 0 should not be null");
				break;
			}

			auto str = v8::JSON::Stringify(args.GetIsolate()->GetCurrentContext(), obj).ToLocalChecked();

			//v8::Local<v8::String> str = v8::JSON::Stringify(args.GetIsolate()->GetCurrentContext()/*context*/, obj).ToLocalChecked();
			v8::String::Utf8Value utf8value(str);
			const char* strdata = ToCString(utf8value);
			Json::Value transaction_json;

			if (!transaction_json.fromCString(strdata)) {
				LOG_ERROR("string to json failed, string=%s", strdata);
				break;
			}

			protocol::Transaction transaction;
			std::string error_msg;
			if (!Json2Proto(transaction_json, transaction, error_msg)) {
				LOG_ERROR("json to protocol object failed: json=%s. error=%s", strdata, error_msg.c_str());
				break;
			}

			transaction.set_source_address(contractor);

			for (int i = 0; i < transaction.operations_size(); i++) {
				protocol::Operation*  ope = transaction.mutable_operations(i);
				ope->set_source_address(contractor);
			}

			//transaction.set_nonce(contract_account->GetAccountNonce());			
			protocol::TransactionEnv env;
			env.mutable_transaction()->CopyFrom(transaction);

			V8Contract *v8_contract = GetContractFrom(args.GetIsolate());
			if (!v8_contract || !v8_contract->parameter_.ledger_context_) {
				LOG_ERROR("Can't find contract object by isolate id");
				break;
			}

			if (v8_contract->IsReadonly()) {
				LOG_ERROR("The contract is readonly");
				break;
			}

			if (!LedgerManager::Instance().DoTransaction(env, v8_contract->parameter_.ledger_context_)) {
				LOG_ERROR("Do transaction failed");
				break;
			}

			args.GetReturnValue().Set(true);
			return;
		} while (false);

		args.GetReturnValue().Set(false);

	}

	QueryContract::QueryContract():contract_(NULL){}
	QueryContract::~QueryContract() {
	}
	bool QueryContract::Init(int32_t type, const ContractParameter &paramter) {
		parameter_ = paramter;
		if (type == Contract::TYPE_V8) {
			
		}
		else {
			std::string error_msg = utils::String::Format("Contract type(%d) not support", type);
			LOG_ERROR("%s", error_msg.c_str());
			return false;
		}
		return true;
	}

	void QueryContract::Cancel() {
		utils::MutexGuard guard(mutex_);
		if (contract_) {
			contract_->Cancel();
		} 
	}

	bool QueryContract::GetResult(Json::Value &result) {
		result = result_;
		return ret_;
	}

	void QueryContract::Run() {
		do {
			utils::MutexGuard guard(mutex_);
			contract_ = new V8Contract(true, parameter_);
		} while (false);

		ret_ = contract_->Query(result_);

		do {
			utils::MutexGuard guard(mutex_);
			delete contract_;
			contract_ = NULL;
		} while (false);
	}

// 	TestContract::TestContract(){}
// 	TestContract::~TestContract() {}
// 	bool TestContract::Init(int32_t type, const ContractTestParameter &parameter) {
// 		if (type == Contract::TYPE_V8) {
// 			parameter_ = parameter;
// 			type_ = type;
// 		}
// 		else {
// 			std::string error_msg = utils::String::Format("Contract type(%d) not support", type);
// 			LOG_ERROR("%s", error_msg.c_str());
// 			return false;
// 		}
// 		return true;
// 	}
// 
// 	void TestContract::Cancel() {
// 		ledger_context.Cancel();
// 	}
// 
// 	bool TestContract::GetResult(Json::Value &result) {
// 		result = result_;
// 		return ret_;
// 	}
// 
// 	void TestContract::Run() {
// 		ledger_context.Test(type_, parameter_);
// 	}

	ContractManager::ContractManager() {}
	ContractManager::~ContractManager() {}

	bool ContractManager::Initialize(int argc, char** argv) {
		V8Contract::Initialize(argc, argv);
		return true;
	}

	bool ContractManager::Exit() {
		return true;
	}

	bool ContractManager::SourceCodeCheck(int32_t type, const std::string &code, std::string &error_msg) {
		ContractParameter parameter;
		parameter.code_ = code;
		Contract *contract = NULL;
		if (type == Contract::TYPE_V8) {
			contract = new V8Contract(false, parameter);
		}
		else {
			error_msg = utils::String::Format("Contract type(%d) not support", type);
			LOG_ERROR("%s", error_msg.c_str());
			return false;
		}

		bool ret = contract->SourceCodeCheck();
		error_msg = contract->GetErrorMsg();
		delete contract;
		return ret;
	}

	bool ContractManager::Execute(int32_t type, const ContractParameter &paramter, std::string &error_msg) {
		do {
			Contract *contract;
			if (type == Contract::TYPE_V8) {
				utils::MutexGuard guard(contracts_lock_);
				contract = new V8Contract(false, paramter);
				//paramter->ledger_context_ 
				//add the contract id for cancel

				contracts_[contract->GetId()] = contract;
			}
			else {
				LOG_ERROR("Contract type(%d) not support", type);
				break;
			}

			LedgerContext *ledger_context = contract->GetParameter().ledger_context_;
			ledger_context->PushContractId(contract->GetId());
			bool ret = contract->Execute();
			ledger_context->PopContractId();
			ledger_context->PushLog(contract->GetParameter().this_address_, contract->GetLogs());
			error_msg = contract->GetErrorMsg();
			do {
				//delete the contract from map
				contracts_.erase(contract->GetId());
				delete contract;
			} while (false);

			return ret;
		} while (false);
		return false;
	}

	bool ContractManager::Query(int32_t type, const ContractParameter &paramter, Json::Value &result) {
		do {
			Contract *contract;
			if (type == Contract::TYPE_V8) {
				utils::MutexGuard guard(contracts_lock_);
				contract = new V8Contract(true, paramter);
				//paramter->ledger_context_ 
				//add the contract id for cancel

				contracts_[contract->GetId()] = contract;
			}
			else {
				LOG_ERROR("Contract type(%d) not support", type);
				break;
			}

			LedgerContext *ledger_context = contract->GetParameter().ledger_context_;
			ledger_context->PushContractId(contract->GetId());
			bool ret = contract->Query(result);
			ledger_context->PopContractId();
			ledger_context->PushLog(contract->GetParameter().this_address_, contract->GetLogs());
			Json::Value ret_obj = Json::Value(Json::objectValue);
			ret_obj = result;
			ret_obj["success"] = ret;
			ledger_context->PushRet(contract->GetParameter().this_address_, ret_obj);
			do {
				//delete the contract from map
				contracts_.erase(contract->GetId());
				delete contract;
			} while (false);

			return ret;
		} while (false);
		return false;
	}

// 	bool ContractManager::Test(int32_t type, const ContractTestParameter &paramter, Json::Value& result) {
// 		TestContract test_contract;
// 		if (!test_contract.Init(type, paramter)) {
// 			return false;
// 		} 
// 
// 		if (!test_contract.Start("query-contract")) {
// 			LOG_ERROR_ERRNO("Start query contract thread failed", STD_ERR_CODE, STD_ERR_DESC);
// 			return false;
// 		} 
// 
// 		int64_t time_start = utils::Timestamp::HighResolution();
// 		bool is_timeout = false;
// 		while (test_contract.IsRunning()) {
// 			utils::Sleep(10);
// 			if (utils::Timestamp::HighResolution() - time_start >  5 * utils::MICRO_UNITS_PER_SEC) {
// 				is_timeout = true;
// 				break;
// 			}
// 		}
// 
// 		if (is_timeout) { //cancel it
// 			test_contract.Cancel();
// 			test_contract.JoinWithStop();
// 		}
// 
// 		return test_contract.GetResult(result);
// 	}

	bool ContractManager::Cancel(int64_t contract_id) {
		//another thread cancel the vm
		Contract *contract = NULL;
		do {
			utils::MutexGuard guard(contracts_lock_);
			ContractMap::iterator iter = contracts_.find(contract_id);
			if (iter!= contracts_.end()) {
				contract = iter->second;
			} 
		} while (false);

		if (contract){
			contract->Cancel();
		} 

		return true;
	}

	Contract *ContractManager::GetContract(int64_t contract_id) {
		do {
			utils::MutexGuard guard(contracts_lock_);
			ContractMap::iterator iter = contracts_.find(contract_id);
			if (iter != contracts_.end()) {
				return iter->second;
			}
		} while (false);
		return NULL;
	}

	//bool ContractManager::DoTransaction(protocol::TransactionEnv& env){
	//	auto back = LedgerManager::Instance().transaction_stack_.second;
	//	std::shared_ptr<AccountFrm> source_account;
	//	back->environment_->GetEntry(env.transaction().source_address(), source_account);
	//	env.mutable_transaction()->set_nonce(source_account->GetAccountNonce() + 1);
	//	auto txfrm = std::make_shared<bubi::TransactionFrm >(env);
	//	//LedgerManager::Instance().execute_transaction_.second = txfrm;

	//	auto header = std::make_shared<protocol::LedgerHeader>(LedgerManager::Instance().closing_ledger_->GetProtoHeader());

	//	if (txfrm->ValidForParameter()){
	//		txfrm->Apply(header, true);
	//	}

	//	if (txfrm->GetResult().code() == protocol::ERRCODE_SUCCESS){
	//		txfrm->AllCommit();
	//	}

	//	//LedgerManager::Instance().execute_transaction_.second = back;
	//	return txfrm->GetResult().code() == protocol::ERRCODE_SUCCESS;
	//}
}
#include "stdafx.h"
#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "CrystalScriptStream.h"
#include "SyntaxParser.h"
#include "ScriptOMAll.h"
#include "GameSession.h"
#include "ResourceMap.h"
#include "ResourceEntity.h"
#include "ResourceBlob.h"
#include "Text.h"
#include "SCO.h"
#include "ScriptText.h"
#include "FileWrite.h"
#include "CompileWrite.h"
#include "format.h"

// The script compile, with no GUI: everything comes from the GameSession.

using namespace std;

bool CompileLog::HasErrors()
{
	return _cErrors > 0;
}

void CompileLog::SummarizeAndReportErrors()
{
	stringstream summaryMessage;
	summaryMessage << _cErrors << " errors, " << _cWarnings << " warnings.";
	ReportResult(CompileResult(summaryMessage.str()));
}

void CompileLog::CalculateErrors()
{
	// Counts the results that the log holds. Each call counts from zero, so
	// a second call does not count a result again.
	_cErrors = (int)count_if(_compileResults.begin(), _compileResults.end(), mem_fun_ref(&CompileResult::IsError));
	_cWarnings = (int)count_if(_compileResults.begin(), _compileResults.end(), mem_fun_ref(&CompileResult::IsWarning));
}

std::unique_ptr<sci::Script> SimpleCompile(const std::unordered_set<std::string> &preProcessorDefines, CompileLog &log, ScriptId &scriptId, bool addCommentsToOM)
{
	std::unique_ptr<sci::Script> script = make_unique<sci::Script>();
	script->SetScriptId(scriptId);
	sci::Result<ScriptText> text = LoadScriptText(scriptId.GetFullPath());
	if (text)
	{
		CScriptStreamLimiter limiter(*text);
		CCrystalScriptStream stream(&limiter);
		if (SyntaxParser_Parse(*script, stream, preProcessorDefines, &log, addCommentsToOM))
		{

		}
	}
	log.CalculateErrors();
	return script;
}

std::unique_ptr<sci::Script> SimpleCompile(const SCIVersion &version, CompileLog &log, ScriptId &scriptId, bool addCommentsToOM)
{
	return SimpleCompile(PreProcessorDefinesFromSCIVersion(version), log, scriptId, addCommentsToOM);
}

// A write that failed: an error in the compile log, which names the script.
static void _ReportWriteError(CompileLog &log, ScriptId &script, const sci::Error &error)
{
	log.ReportResult(CompileResult(fmt::format("Could not write the output of {0}: {1}", script.GetFileNameOrig(), error.ToString()),
		CompileResult::CompileResultType::CRT_Error));
}

sci::Status CompileScriptFile(GameSession &session, CompileResults &results, CompileLog &log, CompileTables &tables, PrecompiledHeaders &headers, ScriptId &script,
	const CompileWriteOptions &options)
{
	CResourceMap &resourceMap = session.ResourceMap();
	const GameFolderHelper &helper = session.Helper();

	g_compileIOTimer.Start();
	sci::Result<ScriptText> text = LoadScriptText(script.GetFullPath());
	g_compileIOTimer.Stop();
	if (!text)
	{
		log.ReportResult(CompileResult(fmt::format("Could not read {0}: {1}", script.GetFileNameOrig(), text.error().ToString()),
			CompileResult::CompileResultType::CRT_Error));
		log.CalculateErrors();
		return sci::Fail(text.error());
	}

	// Until the code is made: the errors are in the log.
	sci::Error compileErrors;
	compileErrors.code = sci::ErrorCode::Compile;
	compileErrors.message = "the script has compile errors";
	compileErrors.where.file = script.GetFullPath();
	sci::Status status = sci::Fail(compileErrors);
	{
		CScriptStreamLimiter limiter(*text);
		CCrystalScriptStream stream(&limiter);

		std::unique_ptr<sci::Script> pScript = std::make_unique<sci::Script>(script);
		// A (GetPoly "name") statement reads the game's polygon files.
		pScript->SetPolyFolder(helper.GetPolyFolder());

		if (SyntaxParser_Parse(*pScript, stream, PreProcessorDefinesFromSCIVersion(session.Version()), &log))
		{
			if (script.GetResourceNumber() != pScript->GetScriptNumber())
			{
				log.ReportResult(
					CompileResult(fmt::format("Script {0} ({1}) declared itself as resource {2}", script.GetResourceNumber(), script.GetTitle(), pScript->GetScriptNumber()),
					CompileResult::CompileResultType::CRT_Warning));
			}

			// Compile and save script resource.
			// Compile our own script!
			if (GenerateScriptResource(session, *pScript, headers, tables, results, helper.GetGenerateDebugInfo()))
			{
				WORD wNum = results.GetScriptNumber();
				// The writes go where the options say. A write that fails is an
				// error, and the compile fails with the first failure.
				status = sci::Ok();
				auto check = [&](const sci::Status &written)
				{
					if (!written)
					{
						_ReportWriteError(log, script, written.error());
						if (status)
						{
							status = written;
						}
					}
				};
				// A resource write, and the list of the written resources.
				auto writeResource = [&](ResourceType type, uint16_t number, const std::vector<uint8_t> &data)
				{
					sci::Status written = WriteCompiledResource(resourceMap, options, type, number, data);
					if (written)
					{
						results.AddWritten(type, number);
					}
					check(written);
				};

				// Save the text resource - but only if it's different than what's there (otherwise needless text resource turds pile up)
				if (!results.GetTextComponent().Texts.empty())
				{
					ResourceEntity &textResource = results.GetTextResource();
					// Mark it as being auto-generated by a script compile:
					textResource.GetComponent<TextComponent>().AddString(AutoGenTextSentinel);

					auto existingTextResource = resourceMap.CreateResourceFromNumber(ResourceType::Text, textResource.ResourceNumber);
					if (!existingTextResource || !existingTextResource->GetComponent<TextComponent>().AreTextsEqual(textResource.GetComponent<TextComponent>()))
					{
						sci::ostream textData;
						std::map<BlobKey, uint32_t> propertyBag;
						textResource.WriteTo(textData, true, textResource.ResourceNumber, propertyBag);
						std::vector<uint8_t> textBytes(textData.GetInternalPointer(), textData.GetInternalPointer() + textData.GetDataSize());
						writeResource(ResourceType::Text, (uint16_t)textResource.ResourceNumber, textBytes);
						log.ReportResult(
							CompileResult(fmt::format("Text resource {1} changed. Added {0} entries.", results.GetTextComponent().Texts.size(), textResource.ResourceNumber),
							CompileResult::CompileResultType::CRT_Message)
							);
					} // Else don't save.
				}

				// Save the script resource, and the heap of an SCI1.1 script.
				writeResource(ResourceType::Script, wNum, results.GetScriptResource());
				std::vector<BYTE> &outputHep = results.GetHeapResource();
				if (!outputHep.empty())
				{
					writeResource(ResourceType::Heap, wNum, outputHep);
				}

				// The .sco file and the debug file describe the resources: a
				// script whose resources could not be written gets neither.
				// The .sco is the last write that can fail the script, so a
				// script that fails leaves no new .sco that a later script of a
				// batch could compile against. The debug file comes after it,
				// so a script whose .sco fails leaves no new debug file. A
				// debug file that cannot be written is a warning: the game does
				// not need it.
				g_compileIOTimer.Start();
				g_compileObjFileTimer.Start();
				if (options.writeObjectFile && status)
				{
					bool changed = false;
					check(SaveSCOFile(helper, results.GetSCO(), script, &changed));
					results.SetObjectFileChanged(changed);
				}
				else if (!options.writeObjectFile && !options.writeResources && status)
				{
					// A dry run: the change that a run would make, and the
					// failure of a .sco that a run could not write.
					sci::Result<bool> wouldChange = SCOFileWouldChange(helper, results.GetSCO(), script);
					if (wouldChange)
					{
						results.SetObjectFileChanged(*wouldChange);
					}
					else
					{
						check(sci::Fail(wouldChange.error()));
					}
				}
				g_compileObjFileTimer.Stop();
				g_compileDebugSymbolTimer.Start();
				if (options.writeDebugInfo && status && !results.GetDebugInfo().empty())
				{
					sci::Status wroteDebugFile = WriteBytesToFile(helper.GetScriptDebugFileName(script.GetResourceNumber()), results.GetDebugInfo());
					if (!wroteDebugFile)
					{
						log.ReportResult(CompileResult(fmt::format("Could not write the debug file of {0}: {1}", script.GetFileNameOrig(), wroteDebugFile.error().ToString()),
							CompileResult::CompileResultType::CRT_Warning));
					}
				}
				g_compileDebugSymbolTimer.Stop();
				g_compileIOTimer.Stop();
			}
		}
	}
	log.CalculateErrors();
	return status;
}

bool NewCompileScript(GameSession &session, CompileResults &results, CompileLog &log, CompileTables &tables, PrecompiledHeaders &headers, ScriptId &script,
	const CompileWriteOptions &options)
{
	return CompileScriptFile(session, results, log, tables, headers, script, options).has_value();
}

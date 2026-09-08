#include "MainForm.h"

using namespace System;
using namespace System::Windows::Forms;

static int ExportTable(String^ perlFile, String^ outFile)
{
    std::wstring code = HalsteadMetrics::ToStd(IO::File::ReadAllText(perlFile)->Replace(L"\r\n", L"\n"));
    PerlAnalyzer analyzer;
    AnalysisResult r = analyzer.analyze(code);

    System::Text::StringBuilder^ sb = gcnew System::Text::StringBuilder();
    sb->AppendLine(String::Format(L"OPERATORS\t{0}\t{1}", r.eta1, r.N1));
    for (const Entry& e : r.operators)
        sb->AppendLine(String::Format(L"{0}\t{1}", HalsteadMetrics::ToNet(e.name), e.count));
    sb->AppendLine(String::Format(L"OPERANDS\t{0}\t{1}", r.eta2, r.N2));
    for (const Entry& e : r.operands)
        sb->AppendLine(String::Format(L"{0}\t{1}", HalsteadMetrics::ToNet(e.name), e.count));
    sb->AppendLine(String::Format(Globalization::CultureInfo::InvariantCulture,
                                  L"METRICS\t{0}\t{1}\t{2}\t{3}\t{4}\t{5}\t{6:F4}\t{7}",
                                  r.eta1, r.eta2, r.N1, r.N2, r.eta, r.N, r.V, r.lineCount));
    IO::File::WriteAllText(outFile, sb->ToString(), gcnew System::Text::UTF8Encoding(false));
    return 0;
}

[STAThreadAttribute]
int main(cli::array<String^>^ args)
{
    if (args->Length >= 3 && args[1] == L"--export")
    {
        return ExportTable(args[0], args[2]);
    }

    HalsteadMetrics::Native::SetProcessDPIAware();   
    Application::EnableVisualStyles();             
    Application::SetCompatibleTextRenderingDefault(false);

    String^ file = (args->Length > 0) ? args[0] : nullptr;

    HalsteadMetrics::MainForm form(file);
    Application::Run(%form);
    return 0;
}

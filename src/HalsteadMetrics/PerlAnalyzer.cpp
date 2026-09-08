#include "PerlAnalyzer.h"

#include <algorithm>   
#include <cmath>    
#include <map>
#include <set>

namespace  
{
    bool isDigit(wchar_t c)      { return c >= L'0' && c <= L'9'; }
    bool isAlpha(wchar_t c)      { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
    bool isIdentStart(wchar_t c) { return isAlpha(c) || c == L'_' || c > 127; }   // c > 127: буквы не из ASCII
    bool isIdentChar(wchar_t c)  { return isIdentStart(c) || isDigit(c); }
    bool isSpace(wchar_t c)      { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\f' || c == L'\v'; }
    bool isHexDigit(wchar_t c)   { return isDigit(c) || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F'); }

    bool isUpperWord(const std::wstring& w)
    {
        if (w.empty()) return false;
        for (wchar_t c : w)
            if (!((c >= L'A' && c <= L'Z') || c == L'_' || isDigit(c))) return false;
        return true;
    }

    wchar_t closingFor(wchar_t open)
    {
        switch (open)
        {
        case L'(': return L')';
        case L'[': return L']';
        case L'{': return L'}';
        case L'<': return L'>';
        default:   return open;  
        }
    }

    const std::wstring kEllipsis = L"…"; 

    const std::map<std::wstring, std::wstring> kControl = {
        { L"if",      L"if" + kEllipsis + L"elsif" + kEllipsis + L"else" },
        { L"unless",  L"unless" + kEllipsis + L"else" },
        { L"while",   L"while" },
        { L"until",   L"until" },
        { L"for",     L"for" },
        { L"foreach", L"foreach" },
        { L"return",  L"return" },
        { L"last",    L"last" },
        { L"next",    L"next" },
        { L"redo",    L"redo" },
        { L"my",      L"my" },
        { L"our",     L"our" },
        { L"local",   L"local" },
    };

    const std::set<std::wstring> kWordOps = {
        L"and", L"or", L"not", L"xor",
        L"lt", L"gt", L"le", L"ge", L"eq", L"ne", L"cmp",
    };

    const std::set<std::wstring> kBuiltins = {
        L"print", L"printf", L"say", L"push", L"pop", L"shift", L"unshift", L"splice",
        L"reverse", L"sort", L"map", L"grep", L"join", L"split", L"keys", L"values",
        L"each", L"exists", L"delete", L"defined", L"undef", L"scalar", L"wantarray",
        L"length", L"substr", L"index", L"rindex", L"uc", L"lc", L"ucfirst", L"lcfirst",
        L"chomp", L"chop", L"chr", L"ord", L"sprintf", L"abs", L"int", L"sqrt", L"sin",
        L"cos", L"atan2", L"exp", L"log", L"hex", L"oct", L"rand", L"srand", L"open",
        L"close", L"binmode", L"eof", L"readline", L"die", L"warn", L"exit", L"require",
        L"ref", L"bless", L"time", L"localtime", L"gmtime", L"sleep", L"chdir", L"mkdir",
        L"rmdir", L"unlink", L"rename", L"opendir", L"readdir", L"closedir", L"stat",
        L"quotemeta", L"pack", L"unpack", L"system", L"exec", L"wait", L"lcfirst",
        L"pos", L"study", L"lock", L"select", L"getc", L"read", L"seek", L"tell",
        L"truncate", L"utime", L"chmod", L"chown", L"glob", L"local",
    };

    const std::set<std::wstring> kFileHandles = { L"STDIN", L"STDOUT", L"STDERR", L"ARGV", L"DATA" };
}

void PerlAnalyzer::reset(const std::wstring& code)
{
    src = code;
    pos = 0;
    n = src.size();
    prevIsTerm = false;
    atLineStart = true;
    subscriptNext = false;
    pendingParenFold = false;
    pendingBraceFold = false;
    loopParenPending = false;
    braceIsDo = false;
    stopped = false;
    pendingParenKind = Bracket::Func;
    ternaryDepth = 0;
    doStarts.clear();
    parens.clear();
    braces.clear();
    pendingHeredocs.clear();
    userSubs.clear();
    result = AnalysisResult();
}

void PerlAnalyzer::collectUserSubs()
{
    size_t p = 0;
    while ((p = src.find(L"sub", p)) != std::wstring::npos)
    {
        bool wordStart = (p == 0) || !isIdentChar(src[p - 1]);
        size_t q = p + 3;
        if (wordStart && q < n && isSpace(src[q]))
        {
            q = skipSpaces(q);
            std::wstring name = peekWord(q);
            if (!name.empty()) userSubs.push_back(name);
        }
        p += 3;
    }
}

void PerlAnalyzer::addToken(const std::wstring& name, TokenKind kind, size_t start, size_t end)
{
    Token t;
    t.name = name;
    t.kind = kind;
    t.start = static_cast<int>(start);
    t.length = static_cast<int>(end - start);
    result.tokens.push_back(t);
}

void PerlAnalyzer::addOperator(const std::wstring& name, size_t start, size_t end)
{
    addToken(name, TokenKind::Operator, start, end);
}

void PerlAnalyzer::addOperand(const std::wstring& name, size_t start, size_t end)
{
    addToken(name, TokenKind::Operand, start, end);
}

wchar_t PerlAnalyzer::peek(size_t offset) const
{
    return (pos + offset < n) ? src[pos + offset] : L'\0';
}

size_t PerlAnalyzer::skipSpaces(size_t from) const
{
    while (from < n && isSpace(src[from])) from++;
    return from;
}

bool PerlAnalyzer::startsWith(const wchar_t* s) const
{
    size_t i = 0;
    while (s[i] != L'\0')
    {
        if (pos + i >= n || src[pos + i] != s[i]) return false;
        i++;
    }
    return true;
}

std::wstring PerlAnalyzer::peekWord(size_t from) const
{
    size_t p = from;
    if (p >= n || !isIdentStart(src[p])) return L"";
    while (p < n)
    {
        if (isIdentChar(src[p])) p++;
        else if (src[p] == L':' && p + 2 < n && src[p + 1] == L':' && isIdentStart(src[p + 2])) p += 2;
        else break;
    }
    return src.substr(from, p - from);
}

void PerlAnalyzer::skipToEndOfLine()
{
    while (pos < n && src[pos] != L'\n') pos++;
}

void PerlAnalyzer::skipPod()
{
    while (pos < n)
    {
        if (startsWith(L"=cut"))
        {
            skipToEndOfLine();
            return;
        }
        skipToEndOfLine();
        if (pos < n) pos++;  
    }
}

void PerlAnalyzer::skipStatement()
{
    while (pos < n && src[pos] != L';') pos++;
    if (pos < n) pos++;
    prevIsTerm = false;
}

void PerlAnalyzer::readHeredocBodies()
{
    for (const Heredoc& h : pendingHeredocs)
    {
        size_t bodyStart = pos;
        size_t bodyEnd = pos;
        bool found = false;
        while (pos < n)
        {
            size_t lineStart = pos;
            skipToEndOfLine();
            std::wstring line = src.substr(lineStart, pos - lineStart);
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            if (h.trimIndent)
            {
                size_t k = 0;
                while (k < line.size() && (line[k] == L' ' || line[k] == L'\t')) k++;
                line = line.substr(k);
            }
            if (pos < n) pos++;   // '\n'
            if (line == h.tag) { found = true; break; }
            bodyEnd = pos;
        }
        if (!found) bodyEnd = pos;
        addOperand(L"<<" + h.tag, bodyStart, bodyEnd);   // всё тело — одна строковая константа
    }
    pendingHeredocs.clear();
    atLineStart = true;
}

AnalysisResult PerlAnalyzer::analyze(const std::wstring& code)
{
    reset(code);
    collectUserSubs();

    while (pos < n && !stopped)
    {
        wchar_t c = src[pos];

        // 1. Переводы строк и пробелы
        if (c == L'\n')
        {
            pos++;
            atLineStart = true;
            if (!pendingHeredocs.empty()) readHeredocBodies();
            continue;
        }
        if (isSpace(c)) { pos++; continue; }

        // 2. Комментарий до конца строки (и строка #!/usr/bin/perl)
        if (c == L'#') { skipToEndOfLine(); continue; }

        // 3. POD-документация начинается с '=слово' в начале строки
        if (atLineStart && c == L'=' && isIdentStart(peek(1))) { skipPod(); continue; }
        atLineStart = false;

        // 4. Собственно лексемы
        if (isIdentStart(c))                                   { readWord();   continue; }
        if (isDigit(c) || (c == L'.' && isDigit(peek(1)) && !prevIsTerm)) { readNumber(); continue; }
        if (c == L'"' || c == L'\'' || c == L'`')              { readString(); continue; }
        if (c == L'$' || c == L'@' || ((c == L'%' || c == L'&') && !prevIsTerm))
        {
            if (readVariable()) continue;   // если это не переменная — упадём в readPunct
        }
        readPunct();
    }

    finalize();
    return result;
}

// ============================================================================
//  Слова: ключевые слова, функции, квази-кавычки, имена
// ============================================================================

void PerlAnalyzer::readWord()
{
    // Снимаем одноразовые флаги, установленные предыдущей лексемой
    pendingParenFold = false;
    pendingBraceFold = false;
    subscriptNext = false;

    size_t start = pos;
    std::wstring word = peekWord(pos);
    pos += word.size();

    size_t next = skipSpaces(pos);                 // следующий значащий символ
    wchar_t nc = (next < n) ? src[next] : L'\0';
    wchar_t nc2 = (next + 1 < n) ? src[next + 1] : L'\0';

    // __END__ / __DATA__ — дальше идут данные, а не программа
    if (word == L"__END__" || word == L"__DATA__") { stopped = true; return; }

    // Слово перед '=>' Perl сам берёт в кавычки: это строковая константа
    if (nc == L'=' && nc2 == L'>')
    {
        addOperand(L"\"" + word + L"\"", start, pos);
        prevIsTerm = true;
        return;
    }

    // Слово внутри фигурных скобок хеша: $h{key} — тоже строковая константа
    if (nc == L'}' && !braces.empty() && braces.back() == Bracket::Subscript)
    {
        addOperand(L"\"" + word + L"\"", start, pos);
        prevIsTerm = true;
        return;
    }

    // Директивы не входят в раздел операторов (как заголовок программы в Паскале)
    if (word == L"use" || word == L"no" || word == L"package") { skipStatement(); return; }

    // Квази-кавычки: q// qq// qw// qr// m// s/// tr/// y///
    if (readQuoteLike(word, start)) return;

    // Метка перед оператором: OUTER: foreach ... (одиночное ':' не из '::')
    if (nc == L':' && nc2 != L':' && !prevIsTerm && ternaryDepth == 0 && isUpperWord(word))
    {
        pos = next + 1;
        return;
    }

    // Объявление подпрограммы: sub имя { ... }
    if (word == L"sub")
    {
        addOperator(L"sub", start, pos);
        std::wstring name = peekWord(next);
        if (!name.empty()) pos = next + name.size();   // имя при объявлении не считаем
        pendingBraceFold = true;                       // '{' тела входит в оператор sub
        prevIsTerm = false;
        return;
    }

    // else и elsif — части оператора if, отдельно не считаются
    if (word == L"else")  { pendingBraceFold = true; prevIsTerm = false; return; }
    if (word == L"elsif") { pendingParenFold = true; pendingParenKind = Bracket::Control; prevIsTerm = false; return; }

    // do { ... } while (...) — один оператор; решаем, когда закроется '}'
    if (word == L"do")
    {
        if (nc == L'{') { pendingBraceFold = true; braceIsDo = true; doStarts.push_back(start); }
        else addOperator(L"do", start, pos);
        prevIsTerm = false;
        return;
    }
    if (word == L"eval" || word == L"BEGIN" || word == L"END")
    {
        addOperator(word, start, pos);
        if (nc == L'{') pendingBraceFold = true;
        else if (nc == L'(') { pendingParenFold = true; pendingParenKind = Bracket::Func; }
        prevIsTerm = false;
        return;
    }

    // Управляющие операторы и объявления
    auto ctl = kControl.find(word);
    if (ctl != kControl.end())
    {
        addOperator(ctl->second, start, pos);
        if (word == L"if" || word == L"unless" || word == L"while" || word == L"until")
        {
            pendingParenFold = true;                   // (условие) входит в оператор
            pendingParenKind = Bracket::Control;
        }
        else if (word == L"for" || word == L"foreach")
        {
            loopParenPending = true;                   // скобки могут идти после "my $x"
        }
        else if (word == L"my" || word == L"our" || word == L"local")
        {
            pendingParenFold = true;                   // my ($a, $b) — скобки списка
            pendingParenKind = Bracket::Plain;
        }
        else if (word == L"next" || word == L"last" || word == L"redo")
        {
            std::wstring label = peekWord(next);       // next OUTER; — метку не считаем
            if (isUpperWord(label)) pos = next + label.size();
        }
        prevIsTerm = false;
        return;
    }

    // Словесные операторы: and or not xor eq ne lt gt le ge cmp
    if (kWordOps.count(word))
    {
        addOperator(word, start, pos);
        prevIsTerm = false;
        return;
    }

    // x — повторение строки ("-" x 40), только если слева стоит операнд
    if (word == L"x" && prevIsTerm)
    {
        if (peek(0) == L'=' && peek(1) != L'=') { pos++; addOperator(L"x=", start, pos); }
        else addOperator(L"x", start, pos);
        prevIsTerm = false;
        return;
    }

    // Дескрипторы файлов STDIN, STDOUT, STDERR — операнды
    if (kFileHandles.count(word))
    {
        addOperand(word, start, pos);
        prevIsTerm = true;
        return;
    }

    // Имя функции: встроенная, объявленная через sub или любое слово перед '('
    bool isUserSub = std::find(userSubs.begin(), userSubs.end(), word) != userSubs.end();
    if (kBuiltins.count(word) || isUserSub || nc == L'(')
    {
        addOperator(word, start, pos);
        if (nc == L'(')
        {
            pendingParenFold = true;                   // скобки аргументов входят в вызов
            pendingParenKind = Bracket::Func;
        }
        else if (nc == L'{' && (word == L"sort" || word == L"map" || word == L"grep"))
        {
            pendingBraceFold = true;                   // sort { ... } — блок входит в оператор
        }
        prevIsTerm = false;
        return;
    }

    // Остальные слова: ЗАГЛАВНЫМИ — константа (операнд), иначе — вызов функции без скобок
    if (isUpperWord(word)) { addOperand(word, start, pos); prevIsTerm = true; }
    else                   { addOperator(word, start, pos); prevIsTerm = false; }
}

// Квази-кавычки. Возвращает true, если слово оказалось такой конструкцией.
bool PerlAnalyzer::readQuoteLike(const std::wstring& word, size_t wordStart)
{
    static const std::set<std::wstring> kinds = { L"q", L"qq", L"qw", L"qr", L"m", L"s", L"tr", L"y" };
    if (!kinds.count(word)) return false;

    wchar_t d = peek(0);   // разделитель должен идти СРАЗУ после слова
    // Не разделитель: буквы/цифры, пробел, а также символы с другим смыслом
    if (d == L'\0' || isIdentChar(d) || isSpace(d)) return false;
    if (d == L'=' || d == L',' || d == L';' || d == L')' || d == L'}' || d == L']' || d == L'>') return false;

    bool ok = false;
    if (word == L"q" || word == L"qq")           // строка
    {
        std::wstring text = readDelimited(d, ok);
        addOperand(L"\"" + text + L"\"", wordStart, pos);
        prevIsTerm = true;
        return true;
    }
    if (word == L"m" || word == L"qr")           // регулярное выражение
    {
        readRegexLiteral(d, wordStart);
        return true;
    }
    if (word == L"qw")                           // список слов: qw(a b c)
    {
        size_t contentStart = pos + 1;
        std::wstring text = readDelimited(d, ok);
        addOperator(L"qw( )", wordStart, pos);
        size_t i = 0;
        while (i < text.size())
        {
            while (i < text.size() && isSpace(text[i])) i++;
            size_t ws = i;
            while (i < text.size() && !isSpace(text[i])) i++;
            if (i > ws) addOperand(L"\"" + text.substr(ws, i - ws) + L"\"", contentStart + ws, contentStart + i);
        }
        prevIsTerm = true;
        return true;
    }

    // s/шаблон/замена/флаги   tr/что/на что/флаги   (y — синоним tr)
    size_t patStart = pos + 1;
    std::wstring pattern = readDelimited(d, ok);
    size_t patEnd = pos - 1;

    std::wstring repl;
    size_t replStart, replEnd;
    if (closingFor(d) != d)
    {
        // скобочные разделители: вторая часть в своих скобках, возможно после пробелов
        pos = skipSpaces(pos);
        wchar_t d2 = peek(0);
        replStart = pos + 1;
        repl = readDelimited(d2, ok);
        replEnd = pos - 1;
    }
    else
    {
        // тот же разделитель: закрывающий первой части открывает вторую
        pos--;
        replStart = pos + 1;
        repl = readDelimited(d, ok);
        replEnd = pos - 1;
    }
    while (pos < n && isAlpha(src[pos])) pos++;   // флаги (g, i, ...)

    bool isSubst = (word == L"s");
    addOperator(isSubst ? L"s///" : L"tr///", wordStart, wordStart + word.size());
    if (isSubst) addOperand(L"/" + pattern + L"/", patStart, patEnd);
    else         addOperand(L"\"" + pattern + L"\"", patStart, patEnd);
    addOperand(L"\"" + repl + L"\"", replStart, replEnd);
    prevIsTerm = true;
    return true;
}

// Текст между разделителями. pos стоит на открывающем; после вызова — за закрывающим.
std::wstring PerlAnalyzer::readDelimited(wchar_t open, bool& ok)
{
    wchar_t close = closingFor(open);
    bool nested = (close != open);     // (…) [...] {…} <…> могут вкладываться
    bool inClass = false;              // внутри [...] регулярного выражения '/' не завершает
    int depth = 1;
    std::wstring out;
    pos++;
    while (pos < n)
    {
        wchar_t ch = src[pos];
        if (ch == L'\\' && pos + 1 < n)            // экранированный символ: берём оба
        {
            out += ch;
            out += src[pos + 1];
            pos += 2;
            continue;
        }
        if (!nested)
        {
            if (ch == L'[') inClass = true;
            else if (ch == L']') inClass = false;
        }
        if (nested && ch == open) depth++;
        else if (ch == close && !inClass)
        {
            depth--;
            if (depth == 0) { pos++; ok = true; return out; }
        }
        out += ch;
        pos++;
    }
    ok = false;
    return out;
}

// Регулярное выражение /.../флаги — константа, то есть операнд
void PerlAnalyzer::readRegexLiteral(wchar_t delimiter, size_t start)
{
    bool ok = false;
    std::wstring pattern = readDelimited(delimiter, ok);
    size_t flagsStart = pos;
    while (pos < n && isAlpha(src[pos])) pos++;
    addOperand(L"/" + pattern + L"/" + src.substr(flagsStart, pos - flagsStart), start, pos);
    prevIsTerm = true;
}

// ============================================================================
//  Числа и строки
// ============================================================================

void PerlAnalyzer::readNumber()
{
    pendingParenFold = false;
    pendingBraceFold = false;
    subscriptNext = false;

    size_t start = pos;
    if (src[pos] == L'0' && (peek(1) == L'x' || peek(1) == L'X'))          // шестнадцатеричное 0x1F
    {
        pos += 2;
        while (pos < n && (isHexDigit(src[pos]) || src[pos] == L'_')) pos++;
    }
    else if (src[pos] == L'0' && (peek(1) == L'b' || peek(1) == L'B'))     // двоичное 0b101
    {
        pos += 2;
        while (pos < n && (src[pos] == L'0' || src[pos] == L'1' || src[pos] == L'_')) pos++;
    }
    else
    {
        while (pos < n && (isDigit(src[pos]) || src[pos] == L'_')) pos++;
        if (pos < n && src[pos] == L'.' && isDigit(peek(1)))               // дробная часть, но не '..'
        {
            pos++;
            while (pos < n && (isDigit(src[pos]) || src[pos] == L'_')) pos++;
        }
        if (pos < n && (src[pos] == L'e' || src[pos] == L'E'))             // порядок 1e-5
        {
            size_t save = pos;
            pos++;
            if (pos < n && (src[pos] == L'+' || src[pos] == L'-')) pos++;
            if (pos < n && isDigit(src[pos])) { while (pos < n && isDigit(src[pos])) pos++; }
            else pos = save;
        }
    }
    addOperand(src.substr(start, pos - start), start, pos);
    prevIsTerm = true;
}

// Строка в кавычках "..." '...' `...` — одна константа (переменные внутри не выделяем)
void PerlAnalyzer::readString()
{
    pendingParenFold = false;
    pendingBraceFold = false;
    subscriptNext = false;

    wchar_t quote = src[pos];
    size_t start = pos;
    std::wstring content;
    pos++;
    while (pos < n && src[pos] != quote)
    {
        if (src[pos] == L'\\' && pos + 1 < n) { content += src[pos]; content += src[pos + 1]; pos += 2; }
        else                                  { content += src[pos]; pos++; }
    }
    if (pos < n) pos++;   // закрывающая кавычка
    addOperand(L"\"" + content + L"\"", start, pos);   // показываем всегда в двойных кавычках
    prevIsTerm = true;
}

// ============================================================================
//  Переменные: $скаляр @массив %хеш &подпрограмма
// ============================================================================

bool PerlAnalyzer::readVariable()
{
    size_t start = pos;
    wchar_t sigil = src[pos];
    wchar_t c1 = peek(1);

    // Прочитать имя после сигила и вернуть его; pos сдвигается за имя
    auto readName = [&]() -> std::wstring
    {
        std::wstring name = peekWord(pos);
        pos += name.size();
        return name;
    };

    if (sigil == L'$')
    {
        if (c1 == L'#')   // $#массив — индекс последнего элемента: считаем одним операндом
        {
            if (isIdentStart(peek(2)))
            {
                pos += 2;
                readName();
                pendingParenFold = pendingBraceFold = subscriptNext = false;
                addOperand(src.substr(start, pos - start), start, pos);
                prevIsTerm = true;
                return true;
            }
            return false;
        }
        if (isIdentStart(c1))
        {
            pos++;
            std::wstring name = readName();
            pendingParenFold = pendingBraceFold = false;
            std::wstring shown;
            wchar_t after = peek(0);
            if (after == L'[')      { shown = L"@" + name; subscriptNext = true; }   // $a[0] — элемент массива @a
            else if (after == L'{') { shown = L"%" + name; subscriptNext = true; }   // $h{k} — элемент хеша %h
            else                    { shown = L"$" + name; subscriptNext = false; }
            addOperand(shown, start, pos);
            prevIsTerm = true;
            return true;
        }
        if (isDigit(c1))   // $1 $2 — группы регулярного выражения
        {
            pos++;
            while (pos < n && isDigit(src[pos])) pos++;
            pendingParenFold = pendingBraceFold = subscriptNext = false;
            addOperand(src.substr(start, pos - start), start, pos);
            prevIsTerm = true;
            return true;
        }
        static const std::wstring specials = L"!@/\\,;.&0";   // $! $@ $/ $\ $, $; $. $& $0
        if (c1 != L'\0' && specials.find(c1) != std::wstring::npos)
        {
            pos += 2;
            pendingParenFold = pendingBraceFold = subscriptNext = false;
            addOperand(src.substr(start, 2), start, pos);
            prevIsTerm = true;
            return true;
        }
        if (c1 == L'^' && isAlpha(peek(2)))   // $^W и подобные
        {
            pos += 3;
            pendingParenFold = pendingBraceFold = subscriptNext = false;
            addOperand(src.substr(start, 3), start, pos);
            prevIsTerm = true;
            return true;
        }
        if (c1 == L'$' || c1 == L'{')   // $$ref, ${...} — разыменование: сам '$' оператор
        {
            pos++;
            pendingParenFold = pendingBraceFold = subscriptNext = false;
            addOperator(L"$", start, pos);
            prevIsTerm = false;
            return true;
        }
        return false;
    }

    if (sigil == L'@')
    {
        if (isIdentStart(c1))
        {
            pos++;
            std::wstring name = readName();
            pendingParenFold = pendingBraceFold = false;
            std::wstring shown;
            wchar_t after = peek(0);
            if (after == L'{') { shown = L"%" + name; subscriptNext = true; }   // @h{...} — срез хеша
            else if (after == L'[') { shown = L"@" + name; subscriptNext = true; } // @a[...] — срез массива
            else               { shown = L"@" + name; subscriptNext = false; }
            addOperand(shown, start, pos);
            prevIsTerm = true;
            return true;
        }
        if (c1 == L'$' || c1 == L'{')   // @$ref, @{...} — разыменование
        {
            pos++;
            pendingParenFold = pendingBraceFold = subscriptNext = false;
            addOperator(L"@", start, pos);
            prevIsTerm = false;
            return true;
        }
        return false;
    }

    if (sigil == L'%')   // сюда попадаем только в позиции операнда (иначе это остаток от деления)
    {
        if (isIdentStart(c1))
        {
            pos++;
            std::wstring name = readName();
            pendingParenFold = pendingBraceFold = subscriptNext = false;
            addOperand(L"%" + name, start, pos);
            prevIsTerm = true;
            return true;
        }
        if (c1 == L'$' || c1 == L'{')
        {
            pos++;
            pendingParenFold = pendingBraceFold = subscriptNext = false;
            addOperator(L"%", start, pos);
            prevIsTerm = false;
            return true;
        }
        return false;
    }

    if (sigil == L'&')   // &имя — вызов подпрограммы
    {
        if (isIdentStart(c1))
        {
            pos++;
            std::wstring name = readName();
            pendingParenFold = pendingBraceFold = subscriptNext = false;
            addOperator(name, start, pos);
            if (src[skipSpaces(pos)] == L'(') { pendingParenFold = true; pendingParenKind = Bracket::Func; }
            prevIsTerm = false;
            return true;
        }
        if (c1 == L'$' || c1 == L'{')
        {
            pos++;
            pendingParenFold = pendingBraceFold = subscriptNext = false;
            addOperator(L"&", start, pos);
            prevIsTerm = false;
            return true;
        }
        return false;
    }
    return false;
}

// ============================================================================
//  Знаки операций и скобки
// ============================================================================

void PerlAnalyzer::readPunct()
{
    // Одноразовые флаги предыдущей лексемы действуют именно на эту лексему
    bool foldParen = pendingParenFold;
    Bracket parenKind = pendingParenKind;
    bool foldBrace = pendingBraceFold;
    bool subscript = subscriptNext;
    pendingParenFold = false;
    pendingBraceFold = false;
    subscriptNext = false;

    size_t start = pos;
    wchar_t c = src[pos];

    switch (c)
    {
    case L'(':
        pos++;
        if (foldParen)              { parens.push_back(parenKind); }                       // часть предыдущего оператора
        else if (loopParenPending)  { parens.push_back(Bracket::ForHeader); loopParenPending = false; }
        else                        { addOperator(L"( )", start, pos); parens.push_back(Bracket::Group); }
        prevIsTerm = false;
        return;

    case L')':
    {
        pos++;
        Bracket k = parens.empty() ? Bracket::Group : parens.back();
        if (!parens.empty()) parens.pop_back();
        if (k == Bracket::Control || k == Bracket::ForHeader)
        {
            pendingBraceFold = true;   // дальше идёт тело { ... } этого оператора
            prevIsTerm = false;
        }
        else if (k == Bracket::Plain) prevIsTerm = false;   // my (...) = ...
        else prevIsTerm = true;                             // (выражение) или вызов — это значение
        return;
    }

    case L'[':
        pos++;
        addOperator(L"[ ]", start, pos);   // индексация массива или анонимный массив
        braces.push_back(Bracket::Index);
        prevIsTerm = false;
        return;

    case L']':
        pos++;
        if (!braces.empty()) braces.pop_back();
        prevIsTerm = true;
        subscriptNext = true;   // $a[0]{k} — цепочка индексов
        return;

    case L'{':
        pos++;
        if (foldBrace)
        {
            braces.push_back(braceIsDo ? Bracket::DoBlock : Bracket::FoldedBlock);
            braceIsDo = false;
        }
        else if (subscript)
        {
            addOperator(L"{ }", start, pos);   // обращение к элементу хеша
            braces.push_back(Bracket::Subscript);
        }
        else
        {
            addOperator(L"{ }", start, pos);   // анонимный хеш или просто блок
            braces.push_back(Bracket::Block);
        }
        loopParenPending = false;
        prevIsTerm = false;
        return;

    case L'}':
    {
        pos++;
        Bracket k = braces.empty() ? Bracket::Block : braces.back();
        if (!braces.empty()) braces.pop_back();
        if (k == Bracket::DoBlock)
        {
            size_t doStart = doStarts.empty() ? start : doStarts.back();
            if (!doStarts.empty()) doStarts.pop_back();
            size_t next = skipSpaces(pos);
            std::wstring w = peekWord(next);
            if (w == L"while" || w == L"until")
            {
                pos = next + w.size();
                addOperator(L"do" + kEllipsis + w, doStart, doStart + 2);   // do { } while ( ) — один оператор
                pendingParenFold = true;
                pendingParenKind = Bracket::Control;
            }
            else addOperator(L"do { }", doStart, doStart + 2);
            prevIsTerm = false;
            return;
        }
        if (k == Bracket::Subscript) { prevIsTerm = true; subscriptNext = true; return; }
        if (k == Bracket::Block)     { prevIsTerm = true; return; }
        prevIsTerm = false;   // закрылось тело if/sub/цикла — начинается новый оператор
        return;
    }

    case L';':
        pos++;
        if (!parens.empty() && parens.back() == Bracket::ForHeader) { prevIsTerm = false; return; }   // for (a; b; c)
        addOperator(L";", start, pos);
        prevIsTerm = false;
        loopParenPending = false;
        return;

    case L',':
        pos++;              // запятая — разделитель списка, не оператор
        prevIsTerm = false;
        return;

    case L'?':
        pos++;
        addOperator(L"? :", start, pos);   // тернарный оператор: '?' и ':' — один оператор
        ternaryDepth++;
        prevIsTerm = false;
        return;

    case L':':
        if (peek(1) == L':') { pos += 2; return; }   // '::' вне имени — пропускаем
        pos++;
        if (ternaryDepth > 0) ternaryDepth--;
        prevIsTerm = false;
        return;
    }

    // Регулярное выражение /.../ — там, где ожидается операнд (не после операнда)
    if (c == L'/' && !prevIsTerm)
    {
        readRegexLiteral(L'/', start);
        return;
    }

    // '<' там, где ожидается операнд: heredoc <<"EOF" или чтение строки <STDIN>
    if (c == L'<' && !prevIsTerm)
    {
        if (peek(1) == L'<')
        {
            size_t p = pos + 2;
            bool trim = false;
            if (p < n && src[p] == L'~') { trim = true; p++; }
            std::wstring tag;
            if (p < n && (src[p] == L'"' || src[p] == L'\''))
            {
                size_t e = src.find(src[p], p + 1);
                if (e != std::wstring::npos) { tag = src.substr(p + 1, e - p - 1); p = e + 1; }
            }
            else tag = peekWord(p), p += tag.size();
            if (!tag.empty())
            {
                Heredoc h;
                h.tag = tag;
                h.trimIndent = trim;
                h.start = static_cast<int>(start);
                pendingHeredocs.push_back(h);
                pos = p;
                prevIsTerm = true;
                return;
            }
        }
        size_t p = pos + 1;
        if (p < n && src[p] == L'>')   // <> — чтение из стандартного ввода/аргументов
        {
            pos = p + 1;
            addOperator(L"<>", start, pos);
            prevIsTerm = true;
            return;
        }
        if (p < n && (isIdentStart(src[p]) || src[p] == L'$'))   // <STDIN> <$fh>
        {
            size_t q = (src[p] == L'$') ? p + 1 : p;
            std::wstring name = peekWord(q);
            size_t e = q + name.size();
            if (!name.empty() && e < n && src[e] == L'>')
            {
                addOperator(L"<>", start, pos + 1);          // оператор чтения строки
                addOperand(src.substr(p, e - p), p, e);       // дескриптор файла — операнд
                pos = e + 1;
                prevIsTerm = true;
                return;
            }
        }
    }

    // Многосимвольные знаки операций: сначала самые длинные
    static const wchar_t* const multi[] = {
        L"<=>", L"**=", L"||=", L"&&=", L"//=", L"...", L"<<=", L">>=",
        L"=~", L"!~", L"==", L"!=", L"<=", L">=", L"&&", L"||", L"//", L"..", L"->",
        L"++", L"--", L"**", L"+=", L"-=", L"*=", L"/=", L".=", L"%=", L"|=", L"&=",
        L"^=", L"<<", L">>", L"=>", nullptr
    };
    for (int i = 0; multi[i] != nullptr; i++)
    {
        if (startsWith(multi[i]))
        {
            std::wstring op = multi[i];
            pos += op.size();
            if (op == L"=>") { prevIsTerm = false; return; }   // «жирная запятая» — разделитель
            addOperator(op, start, pos);
            if (op == L"->") subscriptNext = true;             // $r->{k}, $r->[0]
            prevIsTerm = false;
            return;
        }
    }

    // Односимвольные знаки операций
    static const std::wstring singles = L"+-*/%.=<>!&|^~\\";
    if (singles.find(c) != std::wstring::npos)
    {
        pos++;
        addOperator(std::wstring(1, c), start, pos);
        prevIsTerm = false;
        return;
    }

    pos++;   // что-то незнакомое — пропускаем
}

// ============================================================================
//  Итог: таблицы уникальных операторов/операндов и метрики
// ============================================================================

void PerlAnalyzer::finalize()
{
    std::map<std::wstring, size_t> opIndex, opdIndex;   // имя -> номер строки в таблице

    for (size_t i = 0; i < result.tokens.size(); i++)
    {
        const Token& t = result.tokens[i];
        std::vector<Entry>& table = (t.kind == TokenKind::Operator) ? result.operators : result.operands;
        std::map<std::wstring, size_t>& index = (t.kind == TokenKind::Operator) ? opIndex : opdIndex;

        auto it = index.find(t.name);
        if (it == index.end())
        {
            Entry e;
            e.name = t.name;
            e.count = 0;
            index[t.name] = table.size();
            table.push_back(e);
            it = index.find(t.name);
        }
        Entry& e = table[it->second];
        e.count++;
        e.tokenIndexes.push_back(static_cast<int>(i));
    }

    // Сортируем по убыванию числа вхождений; при равенстве — в порядке появления
    auto byCountDesc = [](const Entry& a, const Entry& b) { return a.count > b.count; };
    std::stable_sort(result.operators.begin(), result.operators.end(), byCountDesc);
    std::stable_sort(result.operands.begin(),  result.operands.end(),  byCountDesc);

    // Базовые метрики
    result.eta1 = static_cast<int>(result.operators.size());
    result.eta2 = static_cast<int>(result.operands.size());
    result.N1 = 0;
    for (const Entry& e : result.operators) result.N1 += e.count;   // N1 = сумма f1j
    result.N2 = 0;
    for (const Entry& e : result.operands)  result.N2 += e.count;   // N2 = сумма f2i

    // Расширенные метрики
    result.eta = result.eta1 + result.eta2;                          // словарь программы
    result.N   = result.N1 + result.N2;                              // длина программы
    result.V   = (result.eta > 0) ? result.N * std::log2(static_cast<double>(result.eta)) : 0.0;   // объём

    // Число строк текста
    int lines = 0;
    for (wchar_t ch : src) if (ch == L'\n') lines++;
    if (!src.empty() && src.back() != L'\n') lines++;
    result.lineCount = lines;
}

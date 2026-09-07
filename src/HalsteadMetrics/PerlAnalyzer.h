#pragma once
// ============================================================================
//  PerlAnalyzer.h — ядро лабораторной: разбор текста программы на Perl и
//  подсчёт метрик Холстеда.
//
//  Это обычный (native) C++: никаких .NET-типов здесь нет, поэтому этот код
//  можно использовать и в консольной программе, и в Windows Forms.
//
//  Напоминание по синтаксису C++:
//    #pragma once      — заголовок подключается только один раз;
//    std::wstring      — строка из «широких» символов wchar_t (UTF-16 в Windows),
//                        нужна, чтобы позиции символов совпадали с RichTextBox;
//    std::vector<T>    — динамический массив элементов типа T;
//    enum class        — перечисление со своей областью имён (TokenKind::Operator).
// ============================================================================

#include <string>
#include <vector>

// Какой это элемент программы с точки зрения Холстеда
enum class TokenKind
{
    Operator,   // оператор: знак операции, ключевое слово, имя функции, скобки, ';'
    Operand     // операнд: переменная или константа (число, строка, регулярное выражение)
};

// Одна найденная в тексте лексема
struct Token
{
    std::wstring name;   // как показывать в таблице: L"+", L"( )", L"$sum", L"\"text\""
    TokenKind    kind;   // оператор или операнд
    int          start;  // позиция первого символа в тексте программы (для подсветки)
    int          length; // длина фрагмента текста
};

// Строка итоговой таблицы: уникальный оператор/операнд и число его вхождений
struct Entry
{
    std::wstring     name;          // обозначение (например L";" или L"$sum")
    int              count;         // f1j для оператора или f2i для операнда
    std::vector<int> tokenIndexes;  // номера лексем в AnalysisResult::tokens (для подсветки)
};

// Полный результат анализа
struct AnalysisResult
{
    std::vector<Token> tokens;     // все лексемы в порядке появления
    std::vector<Entry> operators;  // уникальные операторы, по убыванию f1j
    std::vector<Entry> operands;   // уникальные операнды, по убыванию f2i

    // Шесть базовых метрик Холстеда (f1j и f2i лежат в operators/operands)
    int eta1 = 0;   // словарь операторов — число уникальных операторов
    int eta2 = 0;   // словарь операндов  — число уникальных операндов
    int N1   = 0;   // общее число операторов = сумма f1j
    int N2   = 0;   // общее число операндов  = сумма f2i

    // Три расширенные (производные) метрики
    int    eta = 0;     // словарь программы  η = η1 + η2
    int    N   = 0;     // длина программы    N = N1 + N2
    double V   = 0.0;   // объём программы    V = N * log2(η)

    int lineCount = 0;  // число строк в тексте (справочно)
};

// Анализатор. Использование:
//     PerlAnalyzer analyzer;
//     AnalysisResult r = analyzer.analyze(text);
class PerlAnalyzer
{
public:
    AnalysisResult analyze(const std::wstring& code);

private:
    // ---------- состояние сканера ----------
    std::wstring src;        // анализируемый текст
    size_t       pos = 0;    // текущая позиция в src
    size_t       n   = 0;    // длина src
    bool prevIsTerm  = false;   // предыдущая лексема — «терм» (операнд или закрывающая скобка)?
    bool atLineStart = true;    // с начала строки ещё не было ничего, кроме пробелов
    bool subscriptNext = false; // следующая '[' или '{' — индекс массива/хеша
    bool pendingParenFold = false; // ближайшая '(' входит в состав предыдущего оператора
    bool pendingBraceFold = false; // ближайшая '{' входит в состав предыдущего оператора
    bool loopParenPending = false; // ждём '(' заголовка for/foreach (может идти после my $x)
    bool braceIsDo   = false;      // ближайшая свёрнутая '{' открывает блок do { }
    bool stopped     = false;      // встретили __END__ — дальше не анализируем

    // Что лежит в стеке скобок. Скобка либо самостоятельный оператор
    // (группировка, индекс, блок), либо «свёрнута» в предыдущий оператор
    // (скобки вызова функции, условия if, блока while и т.п.).
    enum class Bracket
    {
        Group,        // ( ) как оператор группировки
        Func,         // ( ) вызова функции — свёрнуты в имя функции
        Control,      // ( ) условия if/while/... — свёрнуты в ключевое слово
        ForHeader,    // ( ) заголовка for/foreach — свёрнуты; ';' внутри не считаем
        Plain,        // ( ) после my/our/local — свёрнуты
        Index,        // [ ] индексация или анонимный массив — оператор
        Subscript,    // { } обращение к элементу хеша — оператор
        Block,        // { } анонимный хеш или обычный блок — оператор
        FoldedBlock,  // { } тела if/sub/foreach/... — свёрнуты в ключевое слово
        DoBlock       // { } тела do — оператор do…while / do…until / do { }
    };
    std::vector<Bracket> parens;   // стек для ( )
    std::vector<Bracket> braces;   // стек для { } и [ ]
    Bracket pendingParenKind = Bracket::Func;   // какого рода будет свёрнутая '('
    int ternaryDepth = 0;                       // сколько '?' ждут своего ':'
    std::vector<size_t> doStarts;               // позиции слов do для открытых блоков do { }

    struct Heredoc { std::wstring tag; bool trimIndent; int start; };
    std::vector<Heredoc> pendingHeredocs;    // heredoc-и, тела которых начнутся со следующей строки

    std::vector<std::wstring> userSubs;      // имена подпрограмм, объявленных через sub
    AnalysisResult result;

    // ---------- вспомогательные методы ----------
    void reset(const std::wstring& code);
    void collectUserSubs();
    void addToken(const std::wstring& name, TokenKind kind, size_t start, size_t end);
    void addOperator(const std::wstring& name, size_t start, size_t end);
    void addOperand(const std::wstring& name, size_t start, size_t end);

    wchar_t peek(size_t offset = 0) const;      // символ на pos + offset или 0
    size_t  skipSpaces(size_t from) const;      // первая не-пробельная позиция начиная с from
    bool    startsWith(const wchar_t* s) const; // текст в pos начинается с s?
    std::wstring peekWord(size_t from) const;   // идентификатор, начинающийся в from

    void skipToEndOfLine();
    void skipPod();
    void skipStatement();          // пропустить всё до ';' включительно (use strict; и т.п.)
    void readHeredocBodies();

    void readWord();
    void readNumber();
    void readString();
    bool readVariable();
    void readPunct();
    bool readQuoteLike(const std::wstring& word, size_t wordStart);
    std::wstring readDelimited(wchar_t open, bool& ok);   // текст между разделителями
    void readRegexLiteral(wchar_t delimiter, size_t start);

    void finalize();
};

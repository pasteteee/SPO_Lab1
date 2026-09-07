#!/usr/bin/perl
# Учёт успеваемости студентов (консольное приложение)
use strict;
use warnings;

our $PASS_MARK = 4;                 # проходной балл
my %grades = (                      # имя студента => оценки через пробел
    "Ivanov"   => "7 8 6 9",
    "Petrov"   => "3 5 4 6",
    "Sidorova" => "9 10 8 9",
    "Kozlov"   => "2 4 3 5",
);
my @names   = sort keys %grades;    # имена по алфавиту
my $line    = "-" x 40;             # строка-разделитель
my $counter = 0;

# --- Пользовательские подпрограммы ---
sub average {                       # среднее арифметическое списка
    my @list = @_;
    my $sum  = 0;
    foreach my $value (@list) {
        $sum += $value;
    }
    return scalar(@list) > 0 ? $sum / scalar(@list) : 0;
}

sub grade_label {                   # словесная оценка по среднему баллу
    my $avg = shift;
    if    ($avg >= 9)          { return "excellent"; }
    elsif ($avg >= 7)          { return "good"; }
    elsif ($avg >= $PASS_MARK) { return "satisfactory"; }
    else                       { return "fail"; }
}

sub factorial {                     # рекурсия
    my $n = shift;
    return 1 if $n <= 1;
    return $n * factorial($n - 1);
}

sub normalize_name {                # регулярные выражения
    my $name = shift;
    $name =~ s/^\s+|\s+$//g;        # убрать пробелы по краям
    $name =~ tr/a-z/A-Z/;           # в верхний регистр
    return $name;
}

# --- Основная часть ---
print "Enter minimal average (default $PASS_MARK): ";
my $input = <STDIN> // "";
chomp($input);
my $limit = ($input =~ /^\d+(\.\d+)?$/) ? $input : $PASS_MARK;
print "$line\n";
printf("%-10s %-12s %6s  %s\n", "Name", "Marks", "Avg", "Result");
print "$line\n";
foreach my $name (@names) {
    my @marks = split / /, $grades{$name};
    my $avg   = average(@marks);
    next if $avg < $limit;          # пропустить слабых студентов
    $counter++;
    printf("%-10s %-12s %6.2f  %s\n", $name, join(",", @marks),
           $avg, grade_label($avg));
}
print "$line\n";
print "Students above limit: $counter of ", scalar(@names), "\n";

my $i = 0;
while ($i < @names) {               # цикл с предусловием
    my @marks = split / /, $grades{$names[$i]};
    my $best  = 0;
    for (my $j = 0; $j < @marks; $j++) {    # цикл в стиле C
        $best = $marks[$j] if $marks[$j] > $best;
    }
    print "Best mark of $names[$i] is $best\n" unless $best < $PASS_MARK;
    $i++;
}

my @top   = grep { average(split / /, $grades{$_}) >= 8 } @names;
my @upper = map { normalize_name($_) } @top;
print "Top students: ", join(", ", @upper), "\n" if @upper;
my $k = 1;
do {                                # цикл с постусловием
    print "$k! = ", factorial($k), "\n";
    $k += 2;
} while ($k <= 5);
my $total = 0;
until ($total >= 20) {              # цикл until
    $total += 7;
    last if $total % 2 == 0;        # досрочный выход из цикла
}
print "Total = $total, remainder = ", $total % 3, "\n";

my $text = "perl";
$text .= " is " . ("fun" x 2);      # конкатенация и повторение
print uc(substr($text, 0, 4)), " length=", length($text), "\n";
delete $grades{"Kozlov"} if exists $grades{"Kozlov"};
print "Records left: ", scalar(keys %grades), "\n";

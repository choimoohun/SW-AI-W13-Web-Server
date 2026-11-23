use strict;
use warnings;
use File::Temp qw(tempdir);
use IO::Socket::INET;

my $parent = tempdir(CLEANUP => 1);
my $root = "$parent/public";
mkdir $root or die "mkdir: $!";
mkdir "$root/images" or die "mkdir: $!";

my $html = '<h1>Train tickets</h1>';
my $binary = "\x89PNG\x00\xff" x 1600;
for my $fixture (['public/index.html', $html], ['public/images/train.png', $binary], ['private.html', 'private']) {
    open my $file, '>:raw', "$parent/$fixture->[0]" or die "fixture: $!";
    print {$file} $fixture->[1] or die "write fixture: $!";
    close $file or die "close fixture: $!";
}
symlink "$parent/private.html", "$root/link.html" or die "symlink: $!";

my $server;
END {
    if ($server) {
        local $?;
        kill 'TERM', $server;
        waitpid $server, 0;
    }
}
$SIG{ALRM} = sub { die "static file test timed out\n" };
alarm 12;

for my $case (
    ['/index.html?train=101', $root, 200, 'text/html; charset=utf-8', $html],
    ['/', $root, 200, 'text/html; charset=utf-8', $html],
    ['/images/train.png', $root, 200, 'image/png', $binary],
    ['/missing', $root, 404],
    ['/../private.html', $root, 403],
    ['/link.html', $root, 403],
    ['/index.html', "$root/missing", 500],
) {
    my ($target, $docroot, $status, $type, $wanted) = @$case;
    my $held = IO::Socket::INET->new(LocalAddr => '127.0.0.1', LocalPort => 0,
                                     Proto => 'tcp', Listen => 1) or die "test port: $!";
    my $port = $held->sockport;
    close $held;
    $server = fork();
    die "fork: $!" unless defined $server;
    if ($server == 0) {
        exec($ARGV[0], $port, $docroot) or die "exec: $!";
    }

    my $client;
    for (1 .. 40) {
        $client = IO::Socket::INET->new(PeerAddr => '127.0.0.1', PeerPort => $port,
                                        Proto => 'tcp', Timeout => 1);
        last if $client;
        select undef, undef, undef, 0.05;
    }
    die "could not connect\n" unless $client;
    $client->autoflush(1);
    print {$client} "GET $target HTTP/1.1\r\nHost: localhost\r\n\r\n" or die "send: $!";
    my $response = do { local $/; <$client> };
    close $client;
    waitpid $server, 0;
    my $exit_status = $?;
    $server = 0;
    my ($headers, $body) = split /\r\n\r\n/, ($response // ''), 2;
    die "$target: expected status $status\n"
        unless $exit_status == 0 && defined($body) && $headers =~ /^HTTP\/1\.1 $status /;
    die "$target: wrong Content-Length\n"
        unless $headers =~ /\r\nContent-Length: (\d+)(?:\r\n|$)/ && length($body) == $1;
    die "$target: wrong content\n" if defined($wanted)
        && ($body ne $wanted || $headers !~ /\r\nContent-Type: \Q$type\E(?:\r\n|$)/);
}

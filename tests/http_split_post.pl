use strict;
use warnings;
use IO::Socket::INET;

my $server = fork();
die "fork: $!" unless defined $server;
if ($server == 0) {
    exec($ARGV[0]) or die "exec: $!";
}

END {
    if ($server) {
        local $?;
        kill 'TERM', $server;
        waitpid $server, 0;
    }
}
$SIG{ALRM} = sub { die "server timed out\n" };
alarm 8;

my $client;
for (1 .. 40) {
    $client = IO::Socket::INET->new(
        PeerAddr => '127.0.0.1', PeerPort => 8080, Proto => 'tcp', Timeout => 1,
    );
    last if $client;
    select undef, undef, undef, 0.05;
}
die "could not connect to the server\n" unless $client;
$client->autoflush(1);

for my $chunk ("POST /submit HTTP/1.1\r",
               "\nHost: localhost\r\nContent-Length: 5\r\nConnection: keep-alive\r\n\r\nhe",
               "l\0o") {
    print {$client} $chunk or die "send: $!";
    select undef, undef, undef, 0.10;
}

my $response = do { local $/; <$client> };
my ($headers, $body) = split /\r\n\r\n/, ($response // ''), 2;
die "expected HTTP 200, close, and exactly 5 response bytes\n"
    unless defined($body) && $headers =~ /^HTTP\/1\.1 200 OK\r\n/
        && $headers =~ /\r\nContent-Length: 5(?:\r\n|$)/
        && $headers =~ /\r\nConnection: close(?:\r\n|$)/
        && $body eq "hel\0o";

waitpid $server, 0;
my $status = $?;
$server = 0;
die "server did not exit successfully\n" if $status != 0;

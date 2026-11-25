use strict;
use warnings;
use FindBin qw($Bin);
use IO::Socket::INET;
use IO::Select;
use Time::HiRes qw(time sleep);
use POSIX qw(WNOHANG);
use JSON::PP qw(decode_json);
$SIG{PIPE} = 'IGNORE';
my $exe = shift @ARGV or die "usage: $0 web_server\n";
my $parent_pid = $$;
my @reservations = map {
    IO::Socket::INET->new(LocalAddr => '127.0.0.1', LocalPort => 0, Listen => 1)
        or die "reserve test port: $!"
} 1..4;
my ($front, $booking_port, $fault, $fault_port) = map { $_->sockport } @reservations;
close $_ for @reservations;
my @children;
my $failed = 0;
$| = 1;

sub check {
    my ($condition, $label) = @_;
    print($condition ? "PASS $label\n" : "FAIL $label\n");
    $failed++ unless $condition;
}
sub dial {
    return IO::Socket::INET->new(PeerAddr => '127.0.0.1', PeerPort => $_[0], Timeout => 1);
}
sub start {
    my ($port, @mode) = @_;
    my $pid = fork();
    die "fork: $!" unless defined $pid;
    if (!$pid) {
        exec($exe, $port, "$Bin/../public", '127.0.0.1', @mode) or die "exec: $!";
    }
    push @children, $pid;
    for (1..60) {
        if (my $sock = dial($port)) { close $sock; return $pid; }
        sleep .05;
    }
    die "server did not listen on $port";
}
sub stop {
    my ($pid, $signal) = @_;
    my $started = time;
    kill $signal, $pid;
    while (time - $started < 9) {
        if (waitpid($pid, WNOHANG) == $pid) {
            my $status = $?;
            @children = grep { $_ != $pid } @children;
            return ($status, time - $started);
        }
        sleep .03;
    }
    return (-1, time - $started);
}
END {
    kill 'TERM', @children if $$ == $parent_pid && @children;
}
sub send_req {
    my ($port, $request) = @_;
    my $sock = dial($port) or die "connect $port: $!";
    print {$sock} $request or die "write $port: $!";
    return $sock;
}
sub http_result {
    my ($sock, $budget) = @_;
    my $end = time + ($budget // 7);
    my $select = IO::Select->new($sock);
    my $data = '';
    while (time < $end && $select->can_read($end - time)) {
        my $n = sysread($sock, my $buffer, 4096);
        last unless defined $n && $n > 0;
        $data .= $buffer;
    }
    close $sock;
    my ($status) = $data =~ /\AHTTP\/1\.1 (\d{3})/;
    my ($headers, $body) = split /\r\n\r\n/, $data, 2;
    return ($status // 0, $body // '', $headers // '');
}
sub request {
    my ($port, $raw) = @_;
    return http_result(send_req($port, $raw));
}
sub get { return "GET $_[0] HTTP/1.1\r\nHost: localhost\r\n\r\n"; }
sub post {
    my ($body, $type) = @_;
    return "POST /api/reservations HTTP/1.1\r\nHost: localhost\r\nContent-Type: " .
        ($type // 'application/json') . "\r\nContent-Length: " . length($body) . "\r\n\r\n" . $body;
}
sub status_is {
    my ($label, $wanted, $port, $raw) = @_;
    my ($actual) = request($port, $raw);
    check($actual eq $wanted, "$label expected=$wanted actual=$actual");
}
my $backend = start($booking_port, '--booking');
my $main = start($front, '--proxy', '127.0.0.1', $booking_port);
my $booking = '{"trainId":"KTX101","seatId":"1A"}';
my ($before, $json, $headers) = request($front, get('/api/seats'));
check($before == 200 && $headers =~ /Content-Type: application\/json/i &&
      (grep { $_->{id} eq '1A' && $_->{available} } @{decode_json($json)->{seats}}),
      "proxy GET 200 and seat starts available");
status_is('booking success', 201, $front, post($booking));
my ($after, $updated) = request($front, get('/api/seats'));
check($after == 200 && (grep { $_->{id} eq '1A' && !$_->{available} } @{decode_json($updated)->{seats}}),
      'GET after reservation reports unavailable');
status_is('duplicate reservation', 409, $front, post($booking));
status_is('bad reservation JSON', 400, $front, post('{'));
status_is('unknown seat', 404, $front, post('{"trainId":"KTX101","seatId":"9Z"}'));
status_is('wrong media type', 415, $front, post($booking, 'text/plain'));
status_is('unknown API path', 404, $front, get('/api/missing'));
status_is('wrong method on known route', 405, $front, get('/api/reservations'));
status_is('malformed Connection option', 400, $front,
          "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: keep alive\r\n\r\n");
my ($list_status, undef, $list_headers) = request($front,
    "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: Keep-Alive, close\r\n\r\n");
check($list_status == 200 && $list_headers =~ /\r\nConnection: close/i,
      'valid Connection option list keeps single-request close policy');
status_is('missing Host', 400, $front, "GET / HTTP/1.1\r\n\r\n");
status_is('unsupported HTTP version', 505, $front, "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n");
status_is('invalid Content-Length', 400, $front, "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: -1\r\n\r\n");
status_is('body size over 8192', 413, $front, "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 8193\r\n\r\n");
status_is('request target fills header limit', 431, $front, 'GET /' . ('a' x 4091));
status_is('unknown static file', 404, $front, get('/missing-file'));
my $gone = send_req($front, "GET / HTTP/1.1\r\n");
close $gone;
status_is('static GET after client disconnect', 200, $front, get('/'));
my $same = post('{"trainId":"KTX101","seatId":"2A"}');
my $s1 = send_req($front, $same);
my $s2 = send_req($front, $same);
my ($one) = http_result($s1);
my ($two) = http_result($s2);
my @pair = sort ($one, $two);
check(join(',', @pair) eq '201,409', "same-seat concurrent POSTs actual=$one,$two");
my $fault_pid = start($fault, '--proxy', '127.0.0.1', $fault_port);
status_is('closed upstream port', 502, $fault, get('/api/seats'));
status_is('static GET while upstream is down', 200, $fault, get('/'));

my $fixture = IO::Socket::INET->new(LocalAddr => '127.0.0.1', LocalPort => $fault_port,
    Listen => 3, ReuseAddr => 1) or die "fixture: $!";
pipe(my $notified, my $notify) or die "pipe: $!";
my $fixture_pid = fork();
die "fork: $!" unless defined $fixture_pid;
if (!$fixture_pid) {
    close $notified;
    my $empty = $fixture->accept or die 'fixture accept for 204';
    sysread $empty, my $empty_request, 4096;
    print {$empty} "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";
    close $empty;
    my $s = $fixture->accept or die 'fixture accept for delayed response';
    sysread $s, my $seen, 4096;
    syswrite $notify, '1';
    close $notify;
    sleep 5;
    close $s;
    exit 0;
}
push @children, $fixture_pid;
close $notify;
close $fixture;
my ($empty_status, $empty_body, $empty_headers) = request($fault, get('/api/empty'));
check($empty_status == 204 && $empty_body eq '' && $empty_headers !~ /\r\nContent-Length:/i,
      "valid 204 without response body or framing header (actual=$empty_status)");
my $start = time;
my $a = send_req($fault, get('/api/seats'));
die "slow fixture did not get request" unless IO::Select->new($notified)->can_read(2) && sysread($notified, my $flag, 1);
close $notified;
my $b_start = time;
my ($b) = request($fault, get('/'));
my $b_elapsed = time - $b_start;
my $a_still_waiting = !IO::Select->new($a)->can_read(0);
my ($a_status) = http_result($a, 5);
my $a_elapsed = time - $start;
check($b == 200 && $a_still_waiting, sprintf('static B finishes while API A waits, B=%.3fs', $b_elapsed));
check($a_status == 504 && $a_elapsed >= 2.5 && $a_elapsed < 4.5,
    sprintf('slow upstream gets 504, status=%d elapsed=%.3fs', $a_status, $a_elapsed));
stop($fixture_pid, 'TERM');
my ($sigint, $int_time) = stop($fault_pid, 'INT');
check($sigint == 0 && $int_time < 2, sprintf('SIGINT idle exit=0 elapsed=%.3fs', $int_time));

my $partial = "GET / HTTP/1.1\r\nHost: localhost\r\n";
my $held_start = time;
my @held = map { send_req($front, $partial) } 1..4;
sleep .20;
push @held, map { send_req($front, $partial) } 1..8;
sleep .20;
my ($extra, undef, $extra_headers) = request($front, get('/'));
check($extra == 503 && $extra_headers =~ /Retry-After: 1/i,
    "4 busy workers and 8 queued, extra actual=$extra");
my ($timed_out) = http_result(shift @held, 6);
my $held_elapsed = time - $held_start;
check($timed_out == 408 && $held_elapsed >= 4.5 && $held_elapsed < 7,
    sprintf('incomplete request times out with 408 elapsed=%.3fs', $held_elapsed));
close $_ for @held;
sleep .1;
status_is('normal service after overload recovery', 200, $front, get('/'));
my @active = map { send_req($front, $partial) } 1..4;
sleep .20;
my $queued = send_req($front, $partial);
sleep .10;
my $terminating = time;
kill 'TERM', $main;
my $queued_closed = IO::Select->new($queued)->can_read(2) &&
    !sysread($queued, my $ignored, 32);
close $queued;
my ($exit, $wait) = stop($main, 'TERM');
my $elapsed = time - $terminating;
check($queued_closed && $exit == 0 && $elapsed < 7, sprintf('SIGTERM closes queued connection and joins active workers in %.3fs', $elapsed));
close $_ for @active;
stop($backend, 'TERM');
printf "Runtime checks: %d failure(s)\n", $failed;
exit($failed != 0);

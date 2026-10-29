"""Consulta SNTP de laboratorio (RFC 5905), sem alterar o relogio do Windows."""
import socket
import struct
import time

NTP_EPOCH = 2208988800


def ntp_stamp(unix_ns):
    seconds, fraction = divmod(unix_ns, 1_000_000_000)
    return struct.pack('!II', (seconds + NTP_EPOCH) & 0xffffffff,
                       fraction * (1 << 32) // 1_000_000_000)


def unix_ns(stamp, near_ns):
    seconds, fraction = struct.unpack('!II', stamp)
    # Escolhe a era NTP mais proxima do relogio local (inclui rollover de 2036).
    near_seconds = near_ns // 1_000_000_000 + NTP_EPOCH
    era = round((near_seconds - seconds) / (1 << 32))
    return ((seconds + era * (1 << 32) - NTP_EPOCH) * 1_000_000_000
            + fraction * 1_000_000_000 // (1 << 32))


def parse_reply(data, origin, t1, t4, monotonic_elapsed_ns):
    if len(data) < 48 or data[0] & 7 != 4 or (data[0] >> 3) & 7 not in (3, 4):
        raise ValueError('Resposta SNTP invalida')
    if data[0] >> 6 == 3 or not 1 <= data[1] <= 15:
        raise ValueError('Servidor sem sincronizacao ou resposta KoD')
    if data[24:32] != origin:
        raise ValueError('Resposta SNTP nao corresponde a consulta')
    if abs((t4 - t1) - monotonic_elapsed_ns) > 100_000_000:
        raise ValueError('Relogio do PC mudou durante a consulta')
    t2, t3 = unix_ns(data[32:40], t4), unix_ns(data[40:48], t4)
    if t3 < t2 or data[40:48] == bytes(8):
        raise ValueError('Timestamps SNTP invalidos')
    delay = (t4 - t1) - (t3 - t2)
    if delay < -1_000_000:
        raise ValueError('Delay SNTP inconsistente')
    return dict(offset_ns=((t2 - t1) + (t3 - t4)) // 2,
                network_delay_ns=max(delay, 0), pc_wall_ns=t4, stratum=data[1],
                source='SNTP estimate; asymmetry and clock drift not bounded')


def query(server='pool.ntp.org', timeout=3):
    address = socket.gethostbyname(server)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)
        sock.connect((address, 123))
        data = bytearray(48)
        data[0] = 0x23
        t1 = time.time_ns()
        m1 = time.monotonic_ns()
        data[40:48] = ntp_stamp(t1)
        sock.send(data)
        reply = sock.recv(512)
        m4 = time.monotonic_ns()
        t4 = time.time_ns()
    result = parse_reply(reply, data[40:48], t1, t4, m4 - m1)
    result.update(server=server, address=address)
    return result

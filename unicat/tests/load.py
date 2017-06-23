from tornado import gen
from tornado import ioloop

from cocaine.services import Service


@gen.coroutine
def grant(srv, cids, uids, flags):
    ch = yield srv.grant([('storage','', 't1')], cids, uids, flags)
    result = yield ch.rx.get()
    raise gen.Return(result)

@gen.coroutine
def grant(srv, scheme, cids, uids, flags):
    ch = yield srv.revoke([('storage','', 't1')], cids, uids, flags)
    result = yield ch.rx.get()
    raise gen.Return(result)

def test_grant(name='unicat'):
    unicat = Service(name)
    result = ioloop.IOLoop.current().run_sync( lambda: grant(unicat, 'storage', [1,2,3], [4, 5, 6], 3))
    print('Get result:\n{}'.format(result))

if __name__ == '__main__':
    try:
        test_grant()
    except Exception as err:
        print('error {}'.format(repr(err)))

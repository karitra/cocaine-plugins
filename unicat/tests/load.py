from tornado import gen
from tornado import ioloop

from cocaine.services import Service

@gen.coroutine
def grant(srv, urls, cids, uids, flags):
    ch = yield srv.grant(urls, cids, uids, flags)
    result = yield ch.rx.get()
    raise gen.Return(result)

@gen.coroutine
def revoke(srv, urls, cids, uids, flags):
    ch = yield srv.revoke(urls, cids, uids, flags)
    result = yield ch.rx.get()
    raise gen.Return(result)

def test_alter(method, items, cids, uids, flags, name='unicat', times=1):
    unicat = Service(name)
    result = ioloop.IOLoop.current().run_sync( lambda:
        [ method(unicat,
            items, cids, uids, flags) for i in xrange(0,times)])
    print('Get result:\n{}'.format(result))

if __name__ == '__main__':
    try:
        items = [
            # Note: one duplicate!
            ('storage', '', 't1'),
            ('storage', '', 't2'),
            ('storage', '', 't3'),
            ('storage', '', 't1') ]

        test_alter(method=grant, items, [1,2,3], [3,4,5,6], 3)
        test_alter(method=grant, items, [1,2,3], [3,4,5,6], 2)
        # test_alter(method=revoke, )
    except Exception as err:
        print('error {}'.format(repr(err)))

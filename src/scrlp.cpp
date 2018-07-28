#include <scrlp.h>
namespace sc
{

// ====== RLP  =======


// CommonData
template <class T>
inline unsigned bytesRequired(T _i)
{
    static_assert(std::is_same<bigint, T>::value || !std::numeric_limits<T>::is_signed, "only unsigned types or bigint supported"); //bigint does not carry sign bit on shift
    unsigned i = 0;
    for (; _i != 0; ++i, _i >>= 8) {
    }
    return i;
}


std::string escaped(std::string const& _s, bool _all)
{
    static const std::map<char, char> prettyEscapes{{'\r', 'r'}, {'\n', 'n'}, {'\t', 't'}, {'\v', 'v'}};
    std::string ret;
    ret.reserve(_s.size() + 2);
    ret.push_back('"');
    for (auto i : _s)
        if (i == '"' && !_all)
            ret += "\\\"";
        else if (i == '\\' && !_all)
            ret += "\\\\";
        else if (prettyEscapes.count(i) && !_all) {
            ret += '\\';
            ret += prettyEscapes.find(i)->second;
        } else if (i < ' ' || _all) {
            ret += "\\x";
            ret.push_back("0123456789abcdef"[(uint8_t)i / 16]);
            ret.push_back("0123456789abcdef"[(uint8_t)i % 16]);
        } else
            ret.push_back(i);
    ret.push_back('"');
    return ret;
}


// RLP
bytes RLPNull = rlp("");
bytes RLPEmptyList = rlpList();

RLP::RLP(bytesConstRef _d, Strictness _s) : m_data(_d)
{
    if ((_s & FailIfTooBig) && actualSize() < _d.size()) {
        if (_s & ThrowOnFail)
            BOOST_THROW_EXCEPTION(std::range_error("Oversized RLP"));
        else
            m_data.reset();
    }
    if ((_s & FailIfTooSmall) && actualSize() > _d.size()) {
        if (_s & ThrowOnFail)
            BOOST_THROW_EXCEPTION(std::range_error("Undersized RLP"));
        else
            m_data.reset();
    }
}

RLP::iterator& RLP::iterator::operator++()
{
    if (m_remaining) {
        m_currentItem.retarget(m_currentItem.next().data(), m_remaining);
        m_currentItem = m_currentItem.cropped(0, sizeAsEncoded(m_currentItem));
        m_remaining -= std::min<size_t>(m_remaining, m_currentItem.size());
    } else
        m_currentItem.retarget(m_currentItem.next().data(), 0);
    return *this;
}

RLP::iterator::iterator(RLP const& _parent, bool _begin)
{
    if (_begin && _parent.isList()) {
        auto pl = _parent.payload();
        m_currentItem = pl.cropped(0, sizeAsEncoded(pl));
        m_remaining = pl.size() - m_currentItem.size();
    } else {
        m_currentItem = _parent.data().cropped(_parent.data().size());
        m_remaining = 0;
    }
}

RLP RLP::operator[](size_t _i) const
{
    if (_i < m_lastIndex) {
        m_lastEnd = sizeAsEncoded(payload());
        m_lastItem = payload().cropped(0, m_lastEnd);
        m_lastIndex = 0;
    }
    for (; m_lastIndex < _i && m_lastItem.size(); ++m_lastIndex) {
        m_lastItem = payload().cropped(m_lastEnd);
        m_lastItem = m_lastItem.cropped(0, sizeAsEncoded(m_lastItem));
        m_lastEnd += m_lastItem.size();
    }
    return RLP(m_lastItem, ThrowOnFail | FailIfTooSmall);
}

RLPs RLP::toList(int _flags) const
{
    RLPs ret;
    if (!isList()) {
        if (_flags & ThrowOnFail)
            BOOST_THROW_EXCEPTION(std::range_error("Bad Cast"));
        else
            return ret;
    }
    for (auto const& i : *this)
        ret.push_back(i);
    return ret;
}

size_t RLP::actualSize() const
{
    if (isNull())
        return 0;
    if (isSingleByte())
        return 1;
    if (isData() || isList())
        return payloadOffset() + length();
    return 0;
}

void RLP::requireGood() const
{
    if (isNull())
        BOOST_THROW_EXCEPTION(std::range_error("Bad RLP"));
    byte n = m_data[0];
    if (n != c_rlpDataImmLenStart + 1)
        return;
    if (m_data.size() < 2)
        BOOST_THROW_EXCEPTION(std::range_error("Bad RLP"));
    if (m_data[1] < c_rlpDataImmLenStart)
        BOOST_THROW_EXCEPTION(std::range_error("Bad RLP"));
}

bool RLP::isInt() const
{
    if (isNull())
        return false;
    requireGood();
    byte n = m_data[0];
    if (n < c_rlpDataImmLenStart)
        return !!n;
    else if (n == c_rlpDataImmLenStart)
        return true;
    else if (n <= c_rlpDataIndLenZero) {
        if (m_data.size() <= 1)
            BOOST_THROW_EXCEPTION(std::range_error("Bad RLP"));
        return m_data[1] != 0;
    } else if (n < c_rlpListStart) {
        if (m_data.size() <= size_t(1 + n - c_rlpDataIndLenZero))
            BOOST_THROW_EXCEPTION(std::range_error("Bad RLP"));
        return m_data[1 + n - c_rlpDataIndLenZero] != 0;
    } else
        return false;
    return false;
}

size_t RLP::length() const
{
    if (isNull())
        return 0;
    requireGood();
    size_t ret = 0;
    byte const n = m_data[0];
    if (n < c_rlpDataImmLenStart)
        return 1;
    else if (n <= c_rlpDataIndLenZero)
        return n - c_rlpDataImmLenStart;
    else if (n < c_rlpListStart) {
        if (m_data.size() <= size_t(n - c_rlpDataIndLenZero))
            BOOST_THROW_EXCEPTION(std::range_error("Bad RLP"));
        if (m_data.size() > 1)
            if (m_data[1] == 0)
                BOOST_THROW_EXCEPTION(std::range_error("Bad RLP"));
        unsigned lengthSize = n - c_rlpDataIndLenZero;
        if (lengthSize > sizeof(ret))
            // We did not check, but would most probably not fit in our memory.
            BOOST_THROW_EXCEPTION(std::range_error("Undersized RLP"));
        // No leading zeroes.
        if (!m_data[1])
            BOOST_THROW_EXCEPTION(std::range_error("Bad RLP"));
        for (unsigned i = 0; i < lengthSize; ++i)
            ret = (ret << 8) | m_data[i + 1];
        // Must be greater than the limit.
        if (ret < c_rlpListStart - c_rlpDataImmLenStart - c_rlpMaxLengthBytes)
            BOOST_THROW_EXCEPTION(std::range_error("Bad RLP"));
    } else if (n <= c_rlpListIndLenZero)
        return n - c_rlpListStart;
    else {
        unsigned lengthSize = n - c_rlpListIndLenZero;
        if (m_data.size() <= lengthSize)
            BOOST_THROW_EXCEPTION(std::range_error("Bad RLP"));
        if (m_data.size() > 1)
            if (m_data[1] == 0)
                BOOST_THROW_EXCEPTION(std::range_error("Bad RLP"));
        if (lengthSize > sizeof(ret))
            // We did not check, but would most probably not fit in our memory.
            BOOST_THROW_EXCEPTION(std::range_error("Undersized RLP"));
        if (!m_data[1])
            BOOST_THROW_EXCEPTION(std::range_error("Bad RLP"));
        for (unsigned i = 0; i < lengthSize; ++i)
            ret = (ret << 8) | m_data[i + 1];
        if (ret < 0x100 - c_rlpListStart - c_rlpMaxLengthBytes)
            BOOST_THROW_EXCEPTION(std::range_error("Bad RLP"));
    }
    // We have to be able to add payloadOffset to length without overflow.
    // This rejects roughly 4GB-sized RLPs on some platforms.
    if (ret >= std::numeric_limits<size_t>::max() - 0x100)
        BOOST_THROW_EXCEPTION(std::range_error("Undersized RLP"));
    return ret;
}

size_t RLP::items() const
{
    if (isList()) {
        bytesConstRef d = payload();
        size_t i = 0;
        for (; d.size(); ++i)
            d = d.cropped(sizeAsEncoded(d));
        return i;
    }
    return 0;
}

RLPStream& RLPStream::appendRaw(bytesConstRef _s, size_t _itemCount)
{
    m_out.insert(m_out.end(), _s.begin(), _s.end());
    noteAppended(_itemCount);
    return *this;
}

void RLPStream::noteAppended(size_t _itemCount)
{
    if (!_itemCount)
        return;
    //	cdebug << "noteAppended(" << _itemCount << ")";
    while (m_listStack.size()) {
        if (m_listStack.back().first < _itemCount)
            BOOST_THROW_EXCEPTION(std::range_error("itemCount too large"));
        m_listStack.back().first -= _itemCount;
        if (m_listStack.back().first)
            break;
        else {
            auto p = m_listStack.back().second;
            m_listStack.pop_back();
            size_t s = m_out.size() - p; // list size
            auto brs = bytesRequired(s);
            unsigned encodeSize = s < c_rlpListImmLenCount ? 1 : (1 + brs);
            //			cdebug << "s: " << s << ", p: " << p << ", m_out.size(): " << m_out.size() << ", encodeSize: " << encodeSize << " (br: " << brs << ")";
            auto os = m_out.size();
            m_out.resize(os + encodeSize);
            memmove(m_out.data() + p + encodeSize, m_out.data() + p, os - p);
            if (s < c_rlpListImmLenCount)
                m_out[p] = (byte)(c_rlpListStart + s);
            else if (c_rlpListIndLenZero + brs <= 0xff) {
                m_out[p] = (byte)(c_rlpListIndLenZero + brs);
                byte* b = &(m_out[p + brs]);
                for (; s; s >>= 8)
                    *(b--) = (byte)s;
            } else
                BOOST_THROW_EXCEPTION(std::range_error("itemCount too large for RLP"));
        }
        _itemCount = 1; // for all following iterations, we've effectively appended a single item only since we completed a list.
    }
}

RLPStream& RLPStream::appendList(size_t _items)
{
    //	cdebug << "appendList(" << _items << ")";
    if (_items)
        m_listStack.push_back(std::make_pair(_items, m_out.size()));
    else
        appendList(bytes());
    return *this;
}

RLPStream& RLPStream::appendList(bytesConstRef _rlp)
{
    if (_rlp.size() < c_rlpListImmLenCount)
        m_out.push_back((byte)(_rlp.size() + c_rlpListStart));
    else
        pushCount(_rlp.size(), c_rlpListIndLenZero);
    appendRaw(_rlp, 1);
    return *this;
}

RLPStream& RLPStream::append(bytesConstRef _s, bool _compact)
{
    size_t s = _s.size();
    byte const* d = _s.data();
    if (_compact)
        for (size_t i = 0; i < _s.size() && !*d; ++i, --s, ++d) {
        }

    if (s == 1 && *d < c_rlpDataImmLenStart)
        m_out.push_back(*d);
    else {
        if (s < c_rlpDataImmLenCount)
            m_out.push_back((byte)(s + c_rlpDataImmLenStart));
        else
            pushCount(s, c_rlpDataIndLenZero);
        appendRaw(bytesConstRef(d, s), 0);
    }
    noteAppended();
    return *this;
}

RLPStream& RLPStream::append(bigint _i)
{
    if (!_i)
        m_out.push_back(c_rlpDataImmLenStart);
    else if (_i < c_rlpDataImmLenStart)
        m_out.push_back((byte)_i);
    else {
        unsigned br = bytesRequired(_i);
        if (br < c_rlpDataImmLenCount)
            m_out.push_back((byte)(br + c_rlpDataImmLenStart));
        else {
            auto brbr = bytesRequired(br);
            if (c_rlpDataIndLenZero + brbr > 0xff)
                BOOST_THROW_EXCEPTION(std::range_error("Number too large for RLP"));
            m_out.push_back((byte)(c_rlpDataIndLenZero + brbr));
            pushInt(br, brbr);
        }
        pushInt(_i, br);
    }
    noteAppended();
    return *this;
}

void RLPStream::pushCount(size_t _count, byte _base)
{
    auto br = bytesRequired(_count);
    if (int(br) + _base > 0xff)
        BOOST_THROW_EXCEPTION(std::range_error("Count too large for RLP"));
    m_out.push_back((byte)(br + _base)); // max 8 bytes.
    pushInt(_count, br);
}

static void streamOut(std::ostream& _out, RLP const& _d, unsigned _depth = 0)
{
    if (_depth > 64)
        _out << "<max-depth-reached>";
    else if (_d.isNull())
        _out << "null";
    else if (_d.isInt())
        _out << std::showbase << std::hex << std::nouppercase << _d.toInt<bigint>(RLP::LaissezFaire) << std::dec;
    else if (_d.isData())
        _out << escaped(_d.toString(), false);
    else if (_d.isList()) {
        _out << "[";
        int j = 0;
        for (auto i : _d) {
            _out << (j++ ? ", " : " ");
            streamOut(_out, i, _depth + 1);
        }
        _out << " ]";
    }
}

std::ostream& operator<<(std::ostream& _out, RLP const& _d)
{
    streamOut(_out, _d);
    return _out;
}
} // namespace sc
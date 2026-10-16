// A header with content, so that the PCH made of it is a real one: classes, inline functions and templates
// that are instantiated, in the numbers a PCH of a project has. Generated; the content is of no meaning.
#pragma once

struct Record0
{
    int m_Id0;
    double m_Value0;
    char m_Name0[ 12 ];
    Record0 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record0() : m_Id0( 0 ), m_Value0( 0.5 ), m_Next( nullptr ), m_Label( "record0" ), m_Flags0( 0u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name0 ); ++c ) { m_Name0[ c ] = (char)( 65 + ( ( c + 0 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id0 + a * 1 + b - (int)( m_Value0 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 31u ) ^ ( m_Flags0 << 0 ) ^ (unsigned)m_Id0; }
    bool Less( const Record0 & other ) const { return m_Id0 < other.m_Id0 || ( m_Id0 == other.m_Id0 && m_Value0 < other.m_Value0 ); }
};

template <class T> struct Box0
{
    T m_Value;
    T m_Other;
    Box0() : m_Value(), m_Other() {}
    explicit Box0( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box0 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box0<int> IntBox0;
typedef Box0<double> DoubleBox0;
inline int Total0( const IntBox0 & a, const DoubleBox0 & b, const Record0 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 0 ) + (int)r.Mix( 0u );
}
static const int g_Table0[] = { 0, 0, 1, 2, 3, 4 };

struct Record1
{
    int m_Id1;
    double m_Value1;
    char m_Name1[ 16 ];
    Record1 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record1() : m_Id1( 1 ), m_Value1( 1.5 ), m_Next( nullptr ), m_Label( "record1" ), m_Flags1( 3u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name1 ); ++c ) { m_Name1[ c ] = (char)( 65 + ( ( c + 1 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id1 + a * 2 + b - (int)( m_Value1 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 32u ) ^ ( m_Flags1 << 1 ) ^ (unsigned)m_Id1; }
    bool Less( const Record1 & other ) const { return m_Id1 < other.m_Id1 || ( m_Id1 == other.m_Id1 && m_Value1 < other.m_Value1 ); }
};

template <class T> struct Box1
{
    T m_Value;
    T m_Other;
    Box1() : m_Value(), m_Other() {}
    explicit Box1( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box1 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box1<int> IntBox1;
typedef Box1<double> DoubleBox1;
inline int Total1( const IntBox1 & a, const DoubleBox1 & b, const Record1 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 1 ) + (int)r.Mix( 1u );
}
static const int g_Table1[] = { 1, 2, 4, 7, 10, 15 };

struct Record2
{
    int m_Id2;
    double m_Value2;
    char m_Name2[ 20 ];
    Record2 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record2() : m_Id2( 2 ), m_Value2( 2.5 ), m_Next( nullptr ), m_Label( "record2" ), m_Flags2( 6u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name2 ); ++c ) { m_Name2[ c ] = (char)( 65 + ( ( c + 2 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id2 + a * 3 + b - (int)( m_Value2 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 33u ) ^ ( m_Flags2 << 2 ) ^ (unsigned)m_Id2; }
    bool Less( const Record2 & other ) const { return m_Id2 < other.m_Id2 || ( m_Id2 == other.m_Id2 && m_Value2 < other.m_Value2 ); }
};

template <class T> struct Box2
{
    T m_Value;
    T m_Other;
    Box2() : m_Value(), m_Other() {}
    explicit Box2( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box2 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box2<int> IntBox2;
typedef Box2<double> DoubleBox2;
inline int Total2( const IntBox2 & a, const DoubleBox2 & b, const Record2 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 2 ) + (int)r.Mix( 2u );
}
static const int g_Table2[] = { 2, 4, 7, 12, 17, 26 };

struct Record3
{
    int m_Id3;
    double m_Value3;
    char m_Name3[ 24 ];
    Record3 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record3() : m_Id3( 3 ), m_Value3( 3.5 ), m_Next( nullptr ), m_Label( "record3" ), m_Flags3( 9u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name3 ); ++c ) { m_Name3[ c ] = (char)( 65 + ( ( c + 3 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id3 + a * 4 + b - (int)( m_Value3 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 34u ) ^ ( m_Flags3 << 3 ) ^ (unsigned)m_Id3; }
    bool Less( const Record3 & other ) const { return m_Id3 < other.m_Id3 || ( m_Id3 == other.m_Id3 && m_Value3 < other.m_Value3 ); }
};

template <class T> struct Box3
{
    T m_Value;
    T m_Other;
    Box3() : m_Value(), m_Other() {}
    explicit Box3( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box3 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box3<int> IntBox3;
typedef Box3<double> DoubleBox3;
inline int Total3( const IntBox3 & a, const DoubleBox3 & b, const Record3 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 3 ) + (int)r.Mix( 3u );
}
static const int g_Table3[] = { 3, 6, 10, 17, 24, 37 };

struct Record4
{
    int m_Id4;
    double m_Value4;
    char m_Name4[ 28 ];
    Record4 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record4() : m_Id4( 4 ), m_Value4( 4.5 ), m_Next( nullptr ), m_Label( "record4" ), m_Flags4( 12u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name4 ); ++c ) { m_Name4[ c ] = (char)( 65 + ( ( c + 4 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id4 + a * 5 + b - (int)( m_Value4 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 35u ) ^ ( m_Flags4 << 4 ) ^ (unsigned)m_Id4; }
    bool Less( const Record4 & other ) const { return m_Id4 < other.m_Id4 || ( m_Id4 == other.m_Id4 && m_Value4 < other.m_Value4 ); }
};

template <class T> struct Box4
{
    T m_Value;
    T m_Other;
    Box4() : m_Value(), m_Other() {}
    explicit Box4( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box4 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box4<int> IntBox4;
typedef Box4<double> DoubleBox4;
inline int Total4( const IntBox4 & a, const DoubleBox4 & b, const Record4 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 4 ) + (int)r.Mix( 4u );
}
static const int g_Table4[] = { 4, 8, 13, 22, 31, 48 };

struct Record5
{
    int m_Id5;
    double m_Value5;
    char m_Name5[ 12 ];
    Record5 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record5() : m_Id5( 5 ), m_Value5( 5.5 ), m_Next( nullptr ), m_Label( "record5" ), m_Flags5( 15u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name5 ); ++c ) { m_Name5[ c ] = (char)( 65 + ( ( c + 5 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id5 + a * 6 + b - (int)( m_Value5 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 36u ) ^ ( m_Flags5 << 5 ) ^ (unsigned)m_Id5; }
    bool Less( const Record5 & other ) const { return m_Id5 < other.m_Id5 || ( m_Id5 == other.m_Id5 && m_Value5 < other.m_Value5 ); }
};

template <class T> struct Box5
{
    T m_Value;
    T m_Other;
    Box5() : m_Value(), m_Other() {}
    explicit Box5( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box5 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box5<int> IntBox5;
typedef Box5<double> DoubleBox5;
inline int Total5( const IntBox5 & a, const DoubleBox5 & b, const Record5 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 5 ) + (int)r.Mix( 5u );
}
static const int g_Table5[] = { 5, 10, 16, 27, 38, 59 };

struct Record6
{
    int m_Id6;
    double m_Value6;
    char m_Name6[ 16 ];
    Record6 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record6() : m_Id6( 6 ), m_Value6( 6.5 ), m_Next( nullptr ), m_Label( "record6" ), m_Flags6( 18u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name6 ); ++c ) { m_Name6[ c ] = (char)( 65 + ( ( c + 6 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id6 + a * 7 + b - (int)( m_Value6 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 37u ) ^ ( m_Flags6 << 6 ) ^ (unsigned)m_Id6; }
    bool Less( const Record6 & other ) const { return m_Id6 < other.m_Id6 || ( m_Id6 == other.m_Id6 && m_Value6 < other.m_Value6 ); }
};

template <class T> struct Box6
{
    T m_Value;
    T m_Other;
    Box6() : m_Value(), m_Other() {}
    explicit Box6( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box6 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box6<int> IntBox6;
typedef Box6<double> DoubleBox6;
inline int Total6( const IntBox6 & a, const DoubleBox6 & b, const Record6 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 6 ) + (int)r.Mix( 6u );
}
static const int g_Table6[] = { 6, 12, 19, 32, 45, 70 };

struct Record7
{
    int m_Id7;
    double m_Value7;
    char m_Name7[ 20 ];
    Record7 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record7() : m_Id7( 7 ), m_Value7( 7.5 ), m_Next( nullptr ), m_Label( "record7" ), m_Flags7( 21u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name7 ); ++c ) { m_Name7[ c ] = (char)( 65 + ( ( c + 7 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id7 + a * 1 + b - (int)( m_Value7 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 38u ) ^ ( m_Flags7 << 7 ) ^ (unsigned)m_Id7; }
    bool Less( const Record7 & other ) const { return m_Id7 < other.m_Id7 || ( m_Id7 == other.m_Id7 && m_Value7 < other.m_Value7 ); }
};

template <class T> struct Box7
{
    T m_Value;
    T m_Other;
    Box7() : m_Value(), m_Other() {}
    explicit Box7( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box7 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box7<int> IntBox7;
typedef Box7<double> DoubleBox7;
inline int Total7( const IntBox7 & a, const DoubleBox7 & b, const Record7 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 7 ) + (int)r.Mix( 7u );
}
static const int g_Table7[] = { 7, 14, 22, 37, 52, 81 };

struct Record8
{
    int m_Id8;
    double m_Value8;
    char m_Name8[ 24 ];
    Record8 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record8() : m_Id8( 8 ), m_Value8( 8.5 ), m_Next( nullptr ), m_Label( "record8" ), m_Flags8( 24u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name8 ); ++c ) { m_Name8[ c ] = (char)( 65 + ( ( c + 8 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id8 + a * 2 + b - (int)( m_Value8 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 39u ) ^ ( m_Flags8 << 8 ) ^ (unsigned)m_Id8; }
    bool Less( const Record8 & other ) const { return m_Id8 < other.m_Id8 || ( m_Id8 == other.m_Id8 && m_Value8 < other.m_Value8 ); }
};

template <class T> struct Box8
{
    T m_Value;
    T m_Other;
    Box8() : m_Value(), m_Other() {}
    explicit Box8( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box8 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box8<int> IntBox8;
typedef Box8<double> DoubleBox8;
inline int Total8( const IntBox8 & a, const DoubleBox8 & b, const Record8 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 8 ) + (int)r.Mix( 8u );
}
static const int g_Table8[] = { 8, 16, 25, 42, 59, 92 };

struct Record9
{
    int m_Id9;
    double m_Value9;
    char m_Name9[ 28 ];
    Record9 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record9() : m_Id9( 9 ), m_Value9( 9.5 ), m_Next( nullptr ), m_Label( "record9" ), m_Flags9( 27u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name9 ); ++c ) { m_Name9[ c ] = (char)( 65 + ( ( c + 9 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id9 + a * 3 + b - (int)( m_Value9 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 40u ) ^ ( m_Flags9 << 9 ) ^ (unsigned)m_Id9; }
    bool Less( const Record9 & other ) const { return m_Id9 < other.m_Id9 || ( m_Id9 == other.m_Id9 && m_Value9 < other.m_Value9 ); }
};

template <class T> struct Box9
{
    T m_Value;
    T m_Other;
    Box9() : m_Value(), m_Other() {}
    explicit Box9( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box9 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box9<int> IntBox9;
typedef Box9<double> DoubleBox9;
inline int Total9( const IntBox9 & a, const DoubleBox9 & b, const Record9 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 9 ) + (int)r.Mix( 9u );
}
static const int g_Table9[] = { 9, 18, 28, 47, 66, 103 };

struct Record10
{
    int m_Id10;
    double m_Value10;
    char m_Name10[ 12 ];
    Record10 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record10() : m_Id10( 10 ), m_Value10( 10.5 ), m_Next( nullptr ), m_Label( "record10" ), m_Flags10( 30u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name10 ); ++c ) { m_Name10[ c ] = (char)( 65 + ( ( c + 10 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id10 + a * 4 + b - (int)( m_Value10 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 41u ) ^ ( m_Flags10 << 10 ) ^ (unsigned)m_Id10; }
    bool Less( const Record10 & other ) const { return m_Id10 < other.m_Id10 || ( m_Id10 == other.m_Id10 && m_Value10 < other.m_Value10 ); }
};

template <class T> struct Box10
{
    T m_Value;
    T m_Other;
    Box10() : m_Value(), m_Other() {}
    explicit Box10( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box10 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box10<int> IntBox10;
typedef Box10<double> DoubleBox10;
inline int Total10( const IntBox10 & a, const DoubleBox10 & b, const Record10 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 10 ) + (int)r.Mix( 10u );
}
static const int g_Table10[] = { 10, 20, 31, 52, 73, 114 };

struct Record11
{
    int m_Id11;
    double m_Value11;
    char m_Name11[ 16 ];
    Record11 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record11() : m_Id11( 11 ), m_Value11( 11.5 ), m_Next( nullptr ), m_Label( "record11" ), m_Flags0( 33u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name11 ); ++c ) { m_Name11[ c ] = (char)( 65 + ( ( c + 11 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id11 + a * 5 + b - (int)( m_Value11 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 42u ) ^ ( m_Flags0 << 11 ) ^ (unsigned)m_Id11; }
    bool Less( const Record11 & other ) const { return m_Id11 < other.m_Id11 || ( m_Id11 == other.m_Id11 && m_Value11 < other.m_Value11 ); }
};

template <class T> struct Box11
{
    T m_Value;
    T m_Other;
    Box11() : m_Value(), m_Other() {}
    explicit Box11( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box11 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box11<int> IntBox11;
typedef Box11<double> DoubleBox11;
inline int Total11( const IntBox11 & a, const DoubleBox11 & b, const Record11 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 11 ) + (int)r.Mix( 11u );
}
static const int g_Table11[] = { 11, 22, 34, 57, 80, 125 };

struct Record12
{
    int m_Id12;
    double m_Value12;
    char m_Name12[ 20 ];
    Record12 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record12() : m_Id12( 12 ), m_Value12( 12.5 ), m_Next( nullptr ), m_Label( "record12" ), m_Flags1( 36u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name12 ); ++c ) { m_Name12[ c ] = (char)( 65 + ( ( c + 12 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id12 + a * 6 + b - (int)( m_Value12 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 43u ) ^ ( m_Flags1 << 12 ) ^ (unsigned)m_Id12; }
    bool Less( const Record12 & other ) const { return m_Id12 < other.m_Id12 || ( m_Id12 == other.m_Id12 && m_Value12 < other.m_Value12 ); }
};

template <class T> struct Box12
{
    T m_Value;
    T m_Other;
    Box12() : m_Value(), m_Other() {}
    explicit Box12( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box12 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box12<int> IntBox12;
typedef Box12<double> DoubleBox12;
inline int Total12( const IntBox12 & a, const DoubleBox12 & b, const Record12 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 12 ) + (int)r.Mix( 12u );
}
static const int g_Table12[] = { 12, 24, 37, 62, 87, 136 };

struct Record13
{
    int m_Id13;
    double m_Value13;
    char m_Name13[ 24 ];
    Record13 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record13() : m_Id13( 13 ), m_Value13( 13.5 ), m_Next( nullptr ), m_Label( "record13" ), m_Flags2( 39u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name13 ); ++c ) { m_Name13[ c ] = (char)( 65 + ( ( c + 13 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id13 + a * 7 + b - (int)( m_Value13 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 44u ) ^ ( m_Flags2 << 0 ) ^ (unsigned)m_Id13; }
    bool Less( const Record13 & other ) const { return m_Id13 < other.m_Id13 || ( m_Id13 == other.m_Id13 && m_Value13 < other.m_Value13 ); }
};

template <class T> struct Box13
{
    T m_Value;
    T m_Other;
    Box13() : m_Value(), m_Other() {}
    explicit Box13( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box13 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box13<int> IntBox13;
typedef Box13<double> DoubleBox13;
inline int Total13( const IntBox13 & a, const DoubleBox13 & b, const Record13 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 13 ) + (int)r.Mix( 13u );
}
static const int g_Table13[] = { 13, 26, 40, 67, 94, 147 };

struct Record14
{
    int m_Id14;
    double m_Value14;
    char m_Name14[ 28 ];
    Record14 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record14() : m_Id14( 14 ), m_Value14( 14.5 ), m_Next( nullptr ), m_Label( "record14" ), m_Flags3( 42u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name14 ); ++c ) { m_Name14[ c ] = (char)( 65 + ( ( c + 14 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id14 + a * 1 + b - (int)( m_Value14 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 45u ) ^ ( m_Flags3 << 1 ) ^ (unsigned)m_Id14; }
    bool Less( const Record14 & other ) const { return m_Id14 < other.m_Id14 || ( m_Id14 == other.m_Id14 && m_Value14 < other.m_Value14 ); }
};

template <class T> struct Box14
{
    T m_Value;
    T m_Other;
    Box14() : m_Value(), m_Other() {}
    explicit Box14( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box14 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box14<int> IntBox14;
typedef Box14<double> DoubleBox14;
inline int Total14( const IntBox14 & a, const DoubleBox14 & b, const Record14 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 14 ) + (int)r.Mix( 14u );
}
static const int g_Table14[] = { 14, 28, 43, 72, 101, 158 };

struct Record15
{
    int m_Id15;
    double m_Value15;
    char m_Name15[ 12 ];
    Record15 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record15() : m_Id15( 15 ), m_Value15( 15.5 ), m_Next( nullptr ), m_Label( "record15" ), m_Flags4( 45u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name15 ); ++c ) { m_Name15[ c ] = (char)( 65 + ( ( c + 15 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id15 + a * 2 + b - (int)( m_Value15 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 46u ) ^ ( m_Flags4 << 2 ) ^ (unsigned)m_Id15; }
    bool Less( const Record15 & other ) const { return m_Id15 < other.m_Id15 || ( m_Id15 == other.m_Id15 && m_Value15 < other.m_Value15 ); }
};

template <class T> struct Box15
{
    T m_Value;
    T m_Other;
    Box15() : m_Value(), m_Other() {}
    explicit Box15( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box15 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box15<int> IntBox15;
typedef Box15<double> DoubleBox15;
inline int Total15( const IntBox15 & a, const DoubleBox15 & b, const Record15 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 15 ) + (int)r.Mix( 15u );
}
static const int g_Table15[] = { 15, 30, 46, 77, 108, 169 };

struct Record16
{
    int m_Id16;
    double m_Value16;
    char m_Name16[ 16 ];
    Record16 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record16() : m_Id16( 16 ), m_Value16( 16.5 ), m_Next( nullptr ), m_Label( "record16" ), m_Flags5( 48u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name16 ); ++c ) { m_Name16[ c ] = (char)( 65 + ( ( c + 16 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id16 + a * 3 + b - (int)( m_Value16 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 47u ) ^ ( m_Flags5 << 3 ) ^ (unsigned)m_Id16; }
    bool Less( const Record16 & other ) const { return m_Id16 < other.m_Id16 || ( m_Id16 == other.m_Id16 && m_Value16 < other.m_Value16 ); }
};

template <class T> struct Box16
{
    T m_Value;
    T m_Other;
    Box16() : m_Value(), m_Other() {}
    explicit Box16( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box16 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box16<int> IntBox16;
typedef Box16<double> DoubleBox16;
inline int Total16( const IntBox16 & a, const DoubleBox16 & b, const Record16 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 16 ) + (int)r.Mix( 16u );
}
static const int g_Table16[] = { 16, 32, 49, 82, 115, 180 };

struct Record17
{
    int m_Id17;
    double m_Value17;
    char m_Name17[ 20 ];
    Record17 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record17() : m_Id17( 17 ), m_Value17( 17.5 ), m_Next( nullptr ), m_Label( "record17" ), m_Flags6( 51u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name17 ); ++c ) { m_Name17[ c ] = (char)( 65 + ( ( c + 17 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id17 + a * 4 + b - (int)( m_Value17 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 48u ) ^ ( m_Flags6 << 4 ) ^ (unsigned)m_Id17; }
    bool Less( const Record17 & other ) const { return m_Id17 < other.m_Id17 || ( m_Id17 == other.m_Id17 && m_Value17 < other.m_Value17 ); }
};

template <class T> struct Box17
{
    T m_Value;
    T m_Other;
    Box17() : m_Value(), m_Other() {}
    explicit Box17( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box17 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box17<int> IntBox17;
typedef Box17<double> DoubleBox17;
inline int Total17( const IntBox17 & a, const DoubleBox17 & b, const Record17 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 17 ) + (int)r.Mix( 17u );
}
static const int g_Table17[] = { 17, 34, 52, 87, 122, 191 };

struct Record18
{
    int m_Id18;
    double m_Value18;
    char m_Name18[ 24 ];
    Record18 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record18() : m_Id18( 18 ), m_Value18( 18.5 ), m_Next( nullptr ), m_Label( "record18" ), m_Flags7( 54u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name18 ); ++c ) { m_Name18[ c ] = (char)( 65 + ( ( c + 18 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id18 + a * 5 + b - (int)( m_Value18 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 49u ) ^ ( m_Flags7 << 5 ) ^ (unsigned)m_Id18; }
    bool Less( const Record18 & other ) const { return m_Id18 < other.m_Id18 || ( m_Id18 == other.m_Id18 && m_Value18 < other.m_Value18 ); }
};

template <class T> struct Box18
{
    T m_Value;
    T m_Other;
    Box18() : m_Value(), m_Other() {}
    explicit Box18( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box18 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box18<int> IntBox18;
typedef Box18<double> DoubleBox18;
inline int Total18( const IntBox18 & a, const DoubleBox18 & b, const Record18 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 18 ) + (int)r.Mix( 18u );
}
static const int g_Table18[] = { 18, 36, 55, 92, 129, 202 };

struct Record19
{
    int m_Id19;
    double m_Value19;
    char m_Name19[ 28 ];
    Record19 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record19() : m_Id19( 19 ), m_Value19( 19.5 ), m_Next( nullptr ), m_Label( "record19" ), m_Flags8( 57u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name19 ); ++c ) { m_Name19[ c ] = (char)( 65 + ( ( c + 19 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id19 + a * 6 + b - (int)( m_Value19 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 50u ) ^ ( m_Flags8 << 6 ) ^ (unsigned)m_Id19; }
    bool Less( const Record19 & other ) const { return m_Id19 < other.m_Id19 || ( m_Id19 == other.m_Id19 && m_Value19 < other.m_Value19 ); }
};

template <class T> struct Box19
{
    T m_Value;
    T m_Other;
    Box19() : m_Value(), m_Other() {}
    explicit Box19( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box19 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box19<int> IntBox19;
typedef Box19<double> DoubleBox19;
inline int Total19( const IntBox19 & a, const DoubleBox19 & b, const Record19 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 19 ) + (int)r.Mix( 19u );
}
static const int g_Table19[] = { 19, 38, 58, 97, 136, 213 };

struct Record20
{
    int m_Id20;
    double m_Value20;
    char m_Name20[ 12 ];
    Record20 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record20() : m_Id20( 20 ), m_Value20( 20.5 ), m_Next( nullptr ), m_Label( "record20" ), m_Flags9( 60u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name20 ); ++c ) { m_Name20[ c ] = (char)( 65 + ( ( c + 20 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id20 + a * 7 + b - (int)( m_Value20 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 51u ) ^ ( m_Flags9 << 7 ) ^ (unsigned)m_Id20; }
    bool Less( const Record20 & other ) const { return m_Id20 < other.m_Id20 || ( m_Id20 == other.m_Id20 && m_Value20 < other.m_Value20 ); }
};

template <class T> struct Box20
{
    T m_Value;
    T m_Other;
    Box20() : m_Value(), m_Other() {}
    explicit Box20( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box20 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box20<int> IntBox20;
typedef Box20<double> DoubleBox20;
inline int Total20( const IntBox20 & a, const DoubleBox20 & b, const Record20 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 20 ) + (int)r.Mix( 20u );
}
static const int g_Table20[] = { 20, 40, 61, 102, 143, 224 };

struct Record21
{
    int m_Id21;
    double m_Value21;
    char m_Name21[ 16 ];
    Record21 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record21() : m_Id21( 21 ), m_Value21( 21.5 ), m_Next( nullptr ), m_Label( "record21" ), m_Flags10( 63u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name21 ); ++c ) { m_Name21[ c ] = (char)( 65 + ( ( c + 21 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id21 + a * 1 + b - (int)( m_Value21 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 52u ) ^ ( m_Flags10 << 8 ) ^ (unsigned)m_Id21; }
    bool Less( const Record21 & other ) const { return m_Id21 < other.m_Id21 || ( m_Id21 == other.m_Id21 && m_Value21 < other.m_Value21 ); }
};

template <class T> struct Box21
{
    T m_Value;
    T m_Other;
    Box21() : m_Value(), m_Other() {}
    explicit Box21( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box21 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box21<int> IntBox21;
typedef Box21<double> DoubleBox21;
inline int Total21( const IntBox21 & a, const DoubleBox21 & b, const Record21 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 21 ) + (int)r.Mix( 21u );
}
static const int g_Table21[] = { 21, 42, 64, 107, 150, 235 };

struct Record22
{
    int m_Id22;
    double m_Value22;
    char m_Name22[ 20 ];
    Record22 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record22() : m_Id22( 22 ), m_Value22( 22.5 ), m_Next( nullptr ), m_Label( "record22" ), m_Flags0( 66u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name22 ); ++c ) { m_Name22[ c ] = (char)( 65 + ( ( c + 22 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id22 + a * 2 + b - (int)( m_Value22 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 53u ) ^ ( m_Flags0 << 9 ) ^ (unsigned)m_Id22; }
    bool Less( const Record22 & other ) const { return m_Id22 < other.m_Id22 || ( m_Id22 == other.m_Id22 && m_Value22 < other.m_Value22 ); }
};

template <class T> struct Box22
{
    T m_Value;
    T m_Other;
    Box22() : m_Value(), m_Other() {}
    explicit Box22( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box22 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box22<int> IntBox22;
typedef Box22<double> DoubleBox22;
inline int Total22( const IntBox22 & a, const DoubleBox22 & b, const Record22 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 22 ) + (int)r.Mix( 22u );
}
static const int g_Table22[] = { 22, 44, 67, 112, 157, 246 };

struct Record23
{
    int m_Id23;
    double m_Value23;
    char m_Name23[ 24 ];
    Record23 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record23() : m_Id23( 23 ), m_Value23( 23.5 ), m_Next( nullptr ), m_Label( "record23" ), m_Flags1( 69u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name23 ); ++c ) { m_Name23[ c ] = (char)( 65 + ( ( c + 23 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id23 + a * 3 + b - (int)( m_Value23 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 54u ) ^ ( m_Flags1 << 10 ) ^ (unsigned)m_Id23; }
    bool Less( const Record23 & other ) const { return m_Id23 < other.m_Id23 || ( m_Id23 == other.m_Id23 && m_Value23 < other.m_Value23 ); }
};

template <class T> struct Box23
{
    T m_Value;
    T m_Other;
    Box23() : m_Value(), m_Other() {}
    explicit Box23( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box23 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box23<int> IntBox23;
typedef Box23<double> DoubleBox23;
inline int Total23( const IntBox23 & a, const DoubleBox23 & b, const Record23 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 23 ) + (int)r.Mix( 23u );
}
static const int g_Table23[] = { 23, 46, 70, 117, 164, 257 };

struct Record24
{
    int m_Id24;
    double m_Value24;
    char m_Name24[ 28 ];
    Record24 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record24() : m_Id24( 24 ), m_Value24( 24.5 ), m_Next( nullptr ), m_Label( "record24" ), m_Flags2( 72u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name24 ); ++c ) { m_Name24[ c ] = (char)( 65 + ( ( c + 24 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id24 + a * 4 + b - (int)( m_Value24 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 55u ) ^ ( m_Flags2 << 11 ) ^ (unsigned)m_Id24; }
    bool Less( const Record24 & other ) const { return m_Id24 < other.m_Id24 || ( m_Id24 == other.m_Id24 && m_Value24 < other.m_Value24 ); }
};

template <class T> struct Box24
{
    T m_Value;
    T m_Other;
    Box24() : m_Value(), m_Other() {}
    explicit Box24( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box24 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box24<int> IntBox24;
typedef Box24<double> DoubleBox24;
inline int Total24( const IntBox24 & a, const DoubleBox24 & b, const Record24 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 24 ) + (int)r.Mix( 24u );
}
static const int g_Table24[] = { 24, 48, 73, 122, 171, 268 };

struct Record25
{
    int m_Id25;
    double m_Value25;
    char m_Name25[ 12 ];
    Record25 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record25() : m_Id25( 25 ), m_Value25( 25.5 ), m_Next( nullptr ), m_Label( "record25" ), m_Flags3( 75u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name25 ); ++c ) { m_Name25[ c ] = (char)( 65 + ( ( c + 25 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id25 + a * 5 + b - (int)( m_Value25 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 56u ) ^ ( m_Flags3 << 12 ) ^ (unsigned)m_Id25; }
    bool Less( const Record25 & other ) const { return m_Id25 < other.m_Id25 || ( m_Id25 == other.m_Id25 && m_Value25 < other.m_Value25 ); }
};

template <class T> struct Box25
{
    T m_Value;
    T m_Other;
    Box25() : m_Value(), m_Other() {}
    explicit Box25( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box25 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box25<int> IntBox25;
typedef Box25<double> DoubleBox25;
inline int Total25( const IntBox25 & a, const DoubleBox25 & b, const Record25 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 25 ) + (int)r.Mix( 25u );
}
static const int g_Table25[] = { 25, 50, 76, 127, 178, 279 };

struct Record26
{
    int m_Id26;
    double m_Value26;
    char m_Name26[ 16 ];
    Record26 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record26() : m_Id26( 26 ), m_Value26( 26.5 ), m_Next( nullptr ), m_Label( "record26" ), m_Flags4( 78u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name26 ); ++c ) { m_Name26[ c ] = (char)( 65 + ( ( c + 26 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id26 + a * 6 + b - (int)( m_Value26 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 57u ) ^ ( m_Flags4 << 0 ) ^ (unsigned)m_Id26; }
    bool Less( const Record26 & other ) const { return m_Id26 < other.m_Id26 || ( m_Id26 == other.m_Id26 && m_Value26 < other.m_Value26 ); }
};

template <class T> struct Box26
{
    T m_Value;
    T m_Other;
    Box26() : m_Value(), m_Other() {}
    explicit Box26( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box26 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box26<int> IntBox26;
typedef Box26<double> DoubleBox26;
inline int Total26( const IntBox26 & a, const DoubleBox26 & b, const Record26 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 26 ) + (int)r.Mix( 26u );
}
static const int g_Table26[] = { 26, 52, 79, 132, 185, 290 };

struct Record27
{
    int m_Id27;
    double m_Value27;
    char m_Name27[ 20 ];
    Record27 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record27() : m_Id27( 27 ), m_Value27( 27.5 ), m_Next( nullptr ), m_Label( "record27" ), m_Flags5( 81u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name27 ); ++c ) { m_Name27[ c ] = (char)( 65 + ( ( c + 27 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id27 + a * 7 + b - (int)( m_Value27 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 58u ) ^ ( m_Flags5 << 1 ) ^ (unsigned)m_Id27; }
    bool Less( const Record27 & other ) const { return m_Id27 < other.m_Id27 || ( m_Id27 == other.m_Id27 && m_Value27 < other.m_Value27 ); }
};

template <class T> struct Box27
{
    T m_Value;
    T m_Other;
    Box27() : m_Value(), m_Other() {}
    explicit Box27( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box27 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box27<int> IntBox27;
typedef Box27<double> DoubleBox27;
inline int Total27( const IntBox27 & a, const DoubleBox27 & b, const Record27 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 27 ) + (int)r.Mix( 27u );
}
static const int g_Table27[] = { 27, 54, 82, 137, 192, 301 };

struct Record28
{
    int m_Id28;
    double m_Value28;
    char m_Name28[ 24 ];
    Record28 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record28() : m_Id28( 28 ), m_Value28( 28.5 ), m_Next( nullptr ), m_Label( "record28" ), m_Flags6( 84u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name28 ); ++c ) { m_Name28[ c ] = (char)( 65 + ( ( c + 28 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id28 + a * 1 + b - (int)( m_Value28 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 59u ) ^ ( m_Flags6 << 2 ) ^ (unsigned)m_Id28; }
    bool Less( const Record28 & other ) const { return m_Id28 < other.m_Id28 || ( m_Id28 == other.m_Id28 && m_Value28 < other.m_Value28 ); }
};

template <class T> struct Box28
{
    T m_Value;
    T m_Other;
    Box28() : m_Value(), m_Other() {}
    explicit Box28( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box28 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box28<int> IntBox28;
typedef Box28<double> DoubleBox28;
inline int Total28( const IntBox28 & a, const DoubleBox28 & b, const Record28 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 28 ) + (int)r.Mix( 28u );
}
static const int g_Table28[] = { 28, 56, 85, 142, 199, 312 };

struct Record29
{
    int m_Id29;
    double m_Value29;
    char m_Name29[ 28 ];
    Record29 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record29() : m_Id29( 29 ), m_Value29( 29.5 ), m_Next( nullptr ), m_Label( "record29" ), m_Flags7( 87u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name29 ); ++c ) { m_Name29[ c ] = (char)( 65 + ( ( c + 29 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id29 + a * 2 + b - (int)( m_Value29 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 60u ) ^ ( m_Flags7 << 3 ) ^ (unsigned)m_Id29; }
    bool Less( const Record29 & other ) const { return m_Id29 < other.m_Id29 || ( m_Id29 == other.m_Id29 && m_Value29 < other.m_Value29 ); }
};

template <class T> struct Box29
{
    T m_Value;
    T m_Other;
    Box29() : m_Value(), m_Other() {}
    explicit Box29( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box29 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box29<int> IntBox29;
typedef Box29<double> DoubleBox29;
inline int Total29( const IntBox29 & a, const DoubleBox29 & b, const Record29 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 29 ) + (int)r.Mix( 29u );
}
static const int g_Table29[] = { 29, 58, 88, 147, 206, 323 };

struct Record30
{
    int m_Id30;
    double m_Value30;
    char m_Name30[ 12 ];
    Record30 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record30() : m_Id30( 30 ), m_Value30( 30.5 ), m_Next( nullptr ), m_Label( "record30" ), m_Flags8( 90u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name30 ); ++c ) { m_Name30[ c ] = (char)( 65 + ( ( c + 30 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id30 + a * 3 + b - (int)( m_Value30 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 61u ) ^ ( m_Flags8 << 4 ) ^ (unsigned)m_Id30; }
    bool Less( const Record30 & other ) const { return m_Id30 < other.m_Id30 || ( m_Id30 == other.m_Id30 && m_Value30 < other.m_Value30 ); }
};

template <class T> struct Box30
{
    T m_Value;
    T m_Other;
    Box30() : m_Value(), m_Other() {}
    explicit Box30( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box30 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box30<int> IntBox30;
typedef Box30<double> DoubleBox30;
inline int Total30( const IntBox30 & a, const DoubleBox30 & b, const Record30 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 30 ) + (int)r.Mix( 30u );
}
static const int g_Table30[] = { 30, 60, 91, 152, 213, 334 };

struct Record31
{
    int m_Id31;
    double m_Value31;
    char m_Name31[ 16 ];
    Record31 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record31() : m_Id31( 31 ), m_Value31( 31.5 ), m_Next( nullptr ), m_Label( "record31" ), m_Flags9( 93u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name31 ); ++c ) { m_Name31[ c ] = (char)( 65 + ( ( c + 31 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id31 + a * 4 + b - (int)( m_Value31 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 62u ) ^ ( m_Flags9 << 5 ) ^ (unsigned)m_Id31; }
    bool Less( const Record31 & other ) const { return m_Id31 < other.m_Id31 || ( m_Id31 == other.m_Id31 && m_Value31 < other.m_Value31 ); }
};

template <class T> struct Box31
{
    T m_Value;
    T m_Other;
    Box31() : m_Value(), m_Other() {}
    explicit Box31( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box31 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box31<int> IntBox31;
typedef Box31<double> DoubleBox31;
inline int Total31( const IntBox31 & a, const DoubleBox31 & b, const Record31 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 31 ) + (int)r.Mix( 31u );
}
static const int g_Table31[] = { 31, 62, 94, 157, 220, 345 };

struct Record32
{
    int m_Id32;
    double m_Value32;
    char m_Name32[ 20 ];
    Record32 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record32() : m_Id32( 32 ), m_Value32( 32.5 ), m_Next( nullptr ), m_Label( "record32" ), m_Flags10( 96u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name32 ); ++c ) { m_Name32[ c ] = (char)( 65 + ( ( c + 32 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id32 + a * 5 + b - (int)( m_Value32 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 63u ) ^ ( m_Flags10 << 6 ) ^ (unsigned)m_Id32; }
    bool Less( const Record32 & other ) const { return m_Id32 < other.m_Id32 || ( m_Id32 == other.m_Id32 && m_Value32 < other.m_Value32 ); }
};

template <class T> struct Box32
{
    T m_Value;
    T m_Other;
    Box32() : m_Value(), m_Other() {}
    explicit Box32( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box32 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box32<int> IntBox32;
typedef Box32<double> DoubleBox32;
inline int Total32( const IntBox32 & a, const DoubleBox32 & b, const Record32 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 32 ) + (int)r.Mix( 32u );
}
static const int g_Table32[] = { 32, 64, 97, 162, 227, 356 };

struct Record33
{
    int m_Id33;
    double m_Value33;
    char m_Name33[ 24 ];
    Record33 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record33() : m_Id33( 33 ), m_Value33( 33.5 ), m_Next( nullptr ), m_Label( "record33" ), m_Flags0( 99u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name33 ); ++c ) { m_Name33[ c ] = (char)( 65 + ( ( c + 33 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id33 + a * 6 + b - (int)( m_Value33 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 64u ) ^ ( m_Flags0 << 7 ) ^ (unsigned)m_Id33; }
    bool Less( const Record33 & other ) const { return m_Id33 < other.m_Id33 || ( m_Id33 == other.m_Id33 && m_Value33 < other.m_Value33 ); }
};

template <class T> struct Box33
{
    T m_Value;
    T m_Other;
    Box33() : m_Value(), m_Other() {}
    explicit Box33( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box33 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box33<int> IntBox33;
typedef Box33<double> DoubleBox33;
inline int Total33( const IntBox33 & a, const DoubleBox33 & b, const Record33 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 33 ) + (int)r.Mix( 33u );
}
static const int g_Table33[] = { 33, 66, 100, 167, 234, 367 };

struct Record34
{
    int m_Id34;
    double m_Value34;
    char m_Name34[ 28 ];
    Record34 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record34() : m_Id34( 34 ), m_Value34( 34.5 ), m_Next( nullptr ), m_Label( "record34" ), m_Flags1( 102u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name34 ); ++c ) { m_Name34[ c ] = (char)( 65 + ( ( c + 34 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id34 + a * 7 + b - (int)( m_Value34 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 65u ) ^ ( m_Flags1 << 8 ) ^ (unsigned)m_Id34; }
    bool Less( const Record34 & other ) const { return m_Id34 < other.m_Id34 || ( m_Id34 == other.m_Id34 && m_Value34 < other.m_Value34 ); }
};

template <class T> struct Box34
{
    T m_Value;
    T m_Other;
    Box34() : m_Value(), m_Other() {}
    explicit Box34( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box34 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box34<int> IntBox34;
typedef Box34<double> DoubleBox34;
inline int Total34( const IntBox34 & a, const DoubleBox34 & b, const Record34 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 34 ) + (int)r.Mix( 34u );
}
static const int g_Table34[] = { 34, 68, 103, 172, 241, 378 };

struct Record35
{
    int m_Id35;
    double m_Value35;
    char m_Name35[ 12 ];
    Record35 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record35() : m_Id35( 35 ), m_Value35( 35.5 ), m_Next( nullptr ), m_Label( "record35" ), m_Flags2( 105u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name35 ); ++c ) { m_Name35[ c ] = (char)( 65 + ( ( c + 35 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id35 + a * 1 + b - (int)( m_Value35 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 66u ) ^ ( m_Flags2 << 9 ) ^ (unsigned)m_Id35; }
    bool Less( const Record35 & other ) const { return m_Id35 < other.m_Id35 || ( m_Id35 == other.m_Id35 && m_Value35 < other.m_Value35 ); }
};

template <class T> struct Box35
{
    T m_Value;
    T m_Other;
    Box35() : m_Value(), m_Other() {}
    explicit Box35( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box35 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box35<int> IntBox35;
typedef Box35<double> DoubleBox35;
inline int Total35( const IntBox35 & a, const DoubleBox35 & b, const Record35 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 35 ) + (int)r.Mix( 35u );
}
static const int g_Table35[] = { 35, 70, 106, 177, 248, 389 };

struct Record36
{
    int m_Id36;
    double m_Value36;
    char m_Name36[ 16 ];
    Record36 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record36() : m_Id36( 36 ), m_Value36( 36.5 ), m_Next( nullptr ), m_Label( "record36" ), m_Flags3( 108u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name36 ); ++c ) { m_Name36[ c ] = (char)( 65 + ( ( c + 36 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id36 + a * 2 + b - (int)( m_Value36 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 67u ) ^ ( m_Flags3 << 10 ) ^ (unsigned)m_Id36; }
    bool Less( const Record36 & other ) const { return m_Id36 < other.m_Id36 || ( m_Id36 == other.m_Id36 && m_Value36 < other.m_Value36 ); }
};

template <class T> struct Box36
{
    T m_Value;
    T m_Other;
    Box36() : m_Value(), m_Other() {}
    explicit Box36( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box36 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box36<int> IntBox36;
typedef Box36<double> DoubleBox36;
inline int Total36( const IntBox36 & a, const DoubleBox36 & b, const Record36 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 36 ) + (int)r.Mix( 36u );
}
static const int g_Table36[] = { 36, 72, 109, 182, 255, 400 };

struct Record37
{
    int m_Id37;
    double m_Value37;
    char m_Name37[ 20 ];
    Record37 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record37() : m_Id37( 37 ), m_Value37( 37.5 ), m_Next( nullptr ), m_Label( "record37" ), m_Flags4( 111u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name37 ); ++c ) { m_Name37[ c ] = (char)( 65 + ( ( c + 37 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id37 + a * 3 + b - (int)( m_Value37 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 68u ) ^ ( m_Flags4 << 11 ) ^ (unsigned)m_Id37; }
    bool Less( const Record37 & other ) const { return m_Id37 < other.m_Id37 || ( m_Id37 == other.m_Id37 && m_Value37 < other.m_Value37 ); }
};

template <class T> struct Box37
{
    T m_Value;
    T m_Other;
    Box37() : m_Value(), m_Other() {}
    explicit Box37( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box37 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box37<int> IntBox37;
typedef Box37<double> DoubleBox37;
inline int Total37( const IntBox37 & a, const DoubleBox37 & b, const Record37 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 37 ) + (int)r.Mix( 37u );
}
static const int g_Table37[] = { 37, 74, 112, 187, 262, 411 };

struct Record38
{
    int m_Id38;
    double m_Value38;
    char m_Name38[ 24 ];
    Record38 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record38() : m_Id38( 38 ), m_Value38( 38.5 ), m_Next( nullptr ), m_Label( "record38" ), m_Flags5( 114u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name38 ); ++c ) { m_Name38[ c ] = (char)( 65 + ( ( c + 38 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id38 + a * 4 + b - (int)( m_Value38 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 69u ) ^ ( m_Flags5 << 12 ) ^ (unsigned)m_Id38; }
    bool Less( const Record38 & other ) const { return m_Id38 < other.m_Id38 || ( m_Id38 == other.m_Id38 && m_Value38 < other.m_Value38 ); }
};

template <class T> struct Box38
{
    T m_Value;
    T m_Other;
    Box38() : m_Value(), m_Other() {}
    explicit Box38( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box38 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box38<int> IntBox38;
typedef Box38<double> DoubleBox38;
inline int Total38( const IntBox38 & a, const DoubleBox38 & b, const Record38 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 38 ) + (int)r.Mix( 38u );
}
static const int g_Table38[] = { 38, 76, 115, 192, 269, 422 };

struct Record39
{
    int m_Id39;
    double m_Value39;
    char m_Name39[ 28 ];
    Record39 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record39() : m_Id39( 39 ), m_Value39( 39.5 ), m_Next( nullptr ), m_Label( "record39" ), m_Flags6( 117u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name39 ); ++c ) { m_Name39[ c ] = (char)( 65 + ( ( c + 39 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id39 + a * 5 + b - (int)( m_Value39 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 70u ) ^ ( m_Flags6 << 0 ) ^ (unsigned)m_Id39; }
    bool Less( const Record39 & other ) const { return m_Id39 < other.m_Id39 || ( m_Id39 == other.m_Id39 && m_Value39 < other.m_Value39 ); }
};

template <class T> struct Box39
{
    T m_Value;
    T m_Other;
    Box39() : m_Value(), m_Other() {}
    explicit Box39( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box39 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box39<int> IntBox39;
typedef Box39<double> DoubleBox39;
inline int Total39( const IntBox39 & a, const DoubleBox39 & b, const Record39 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 39 ) + (int)r.Mix( 39u );
}
static const int g_Table39[] = { 39, 78, 118, 197, 276, 433 };

struct Record40
{
    int m_Id40;
    double m_Value40;
    char m_Name40[ 12 ];
    Record40 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record40() : m_Id40( 40 ), m_Value40( 40.5 ), m_Next( nullptr ), m_Label( "record40" ), m_Flags7( 120u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name40 ); ++c ) { m_Name40[ c ] = (char)( 65 + ( ( c + 40 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id40 + a * 6 + b - (int)( m_Value40 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 71u ) ^ ( m_Flags7 << 1 ) ^ (unsigned)m_Id40; }
    bool Less( const Record40 & other ) const { return m_Id40 < other.m_Id40 || ( m_Id40 == other.m_Id40 && m_Value40 < other.m_Value40 ); }
};

template <class T> struct Box40
{
    T m_Value;
    T m_Other;
    Box40() : m_Value(), m_Other() {}
    explicit Box40( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box40 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box40<int> IntBox40;
typedef Box40<double> DoubleBox40;
inline int Total40( const IntBox40 & a, const DoubleBox40 & b, const Record40 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 40 ) + (int)r.Mix( 40u );
}
static const int g_Table40[] = { 40, 80, 121, 202, 283, 444 };

struct Record41
{
    int m_Id41;
    double m_Value41;
    char m_Name41[ 16 ];
    Record41 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record41() : m_Id41( 41 ), m_Value41( 41.5 ), m_Next( nullptr ), m_Label( "record41" ), m_Flags8( 123u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name41 ); ++c ) { m_Name41[ c ] = (char)( 65 + ( ( c + 41 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id41 + a * 7 + b - (int)( m_Value41 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 72u ) ^ ( m_Flags8 << 2 ) ^ (unsigned)m_Id41; }
    bool Less( const Record41 & other ) const { return m_Id41 < other.m_Id41 || ( m_Id41 == other.m_Id41 && m_Value41 < other.m_Value41 ); }
};

template <class T> struct Box41
{
    T m_Value;
    T m_Other;
    Box41() : m_Value(), m_Other() {}
    explicit Box41( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box41 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box41<int> IntBox41;
typedef Box41<double> DoubleBox41;
inline int Total41( const IntBox41 & a, const DoubleBox41 & b, const Record41 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 41 ) + (int)r.Mix( 41u );
}
static const int g_Table41[] = { 41, 82, 124, 207, 290, 455 };

struct Record42
{
    int m_Id42;
    double m_Value42;
    char m_Name42[ 20 ];
    Record42 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record42() : m_Id42( 42 ), m_Value42( 42.5 ), m_Next( nullptr ), m_Label( "record42" ), m_Flags9( 126u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name42 ); ++c ) { m_Name42[ c ] = (char)( 65 + ( ( c + 42 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id42 + a * 1 + b - (int)( m_Value42 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 73u ) ^ ( m_Flags9 << 3 ) ^ (unsigned)m_Id42; }
    bool Less( const Record42 & other ) const { return m_Id42 < other.m_Id42 || ( m_Id42 == other.m_Id42 && m_Value42 < other.m_Value42 ); }
};

template <class T> struct Box42
{
    T m_Value;
    T m_Other;
    Box42() : m_Value(), m_Other() {}
    explicit Box42( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box42 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box42<int> IntBox42;
typedef Box42<double> DoubleBox42;
inline int Total42( const IntBox42 & a, const DoubleBox42 & b, const Record42 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 42 ) + (int)r.Mix( 42u );
}
static const int g_Table42[] = { 42, 84, 127, 212, 297, 466 };

struct Record43
{
    int m_Id43;
    double m_Value43;
    char m_Name43[ 24 ];
    Record43 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record43() : m_Id43( 43 ), m_Value43( 43.5 ), m_Next( nullptr ), m_Label( "record43" ), m_Flags10( 129u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name43 ); ++c ) { m_Name43[ c ] = (char)( 65 + ( ( c + 43 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id43 + a * 2 + b - (int)( m_Value43 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 74u ) ^ ( m_Flags10 << 4 ) ^ (unsigned)m_Id43; }
    bool Less( const Record43 & other ) const { return m_Id43 < other.m_Id43 || ( m_Id43 == other.m_Id43 && m_Value43 < other.m_Value43 ); }
};

template <class T> struct Box43
{
    T m_Value;
    T m_Other;
    Box43() : m_Value(), m_Other() {}
    explicit Box43( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box43 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box43<int> IntBox43;
typedef Box43<double> DoubleBox43;
inline int Total43( const IntBox43 & a, const DoubleBox43 & b, const Record43 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 43 ) + (int)r.Mix( 43u );
}
static const int g_Table43[] = { 43, 86, 130, 217, 304, 477 };

struct Record44
{
    int m_Id44;
    double m_Value44;
    char m_Name44[ 28 ];
    Record44 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record44() : m_Id44( 44 ), m_Value44( 44.5 ), m_Next( nullptr ), m_Label( "record44" ), m_Flags0( 132u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name44 ); ++c ) { m_Name44[ c ] = (char)( 65 + ( ( c + 44 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id44 + a * 3 + b - (int)( m_Value44 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 75u ) ^ ( m_Flags0 << 5 ) ^ (unsigned)m_Id44; }
    bool Less( const Record44 & other ) const { return m_Id44 < other.m_Id44 || ( m_Id44 == other.m_Id44 && m_Value44 < other.m_Value44 ); }
};

template <class T> struct Box44
{
    T m_Value;
    T m_Other;
    Box44() : m_Value(), m_Other() {}
    explicit Box44( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box44 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box44<int> IntBox44;
typedef Box44<double> DoubleBox44;
inline int Total44( const IntBox44 & a, const DoubleBox44 & b, const Record44 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 44 ) + (int)r.Mix( 44u );
}
static const int g_Table44[] = { 44, 88, 133, 222, 311, 488 };

struct Record45
{
    int m_Id45;
    double m_Value45;
    char m_Name45[ 12 ];
    Record45 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record45() : m_Id45( 45 ), m_Value45( 45.5 ), m_Next( nullptr ), m_Label( "record45" ), m_Flags1( 135u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name45 ); ++c ) { m_Name45[ c ] = (char)( 65 + ( ( c + 45 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id45 + a * 4 + b - (int)( m_Value45 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 76u ) ^ ( m_Flags1 << 6 ) ^ (unsigned)m_Id45; }
    bool Less( const Record45 & other ) const { return m_Id45 < other.m_Id45 || ( m_Id45 == other.m_Id45 && m_Value45 < other.m_Value45 ); }
};

template <class T> struct Box45
{
    T m_Value;
    T m_Other;
    Box45() : m_Value(), m_Other() {}
    explicit Box45( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box45 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box45<int> IntBox45;
typedef Box45<double> DoubleBox45;
inline int Total45( const IntBox45 & a, const DoubleBox45 & b, const Record45 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 45 ) + (int)r.Mix( 45u );
}
static const int g_Table45[] = { 45, 90, 136, 227, 318, 499 };

struct Record46
{
    int m_Id46;
    double m_Value46;
    char m_Name46[ 16 ];
    Record46 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record46() : m_Id46( 46 ), m_Value46( 46.5 ), m_Next( nullptr ), m_Label( "record46" ), m_Flags2( 138u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name46 ); ++c ) { m_Name46[ c ] = (char)( 65 + ( ( c + 46 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id46 + a * 5 + b - (int)( m_Value46 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 77u ) ^ ( m_Flags2 << 7 ) ^ (unsigned)m_Id46; }
    bool Less( const Record46 & other ) const { return m_Id46 < other.m_Id46 || ( m_Id46 == other.m_Id46 && m_Value46 < other.m_Value46 ); }
};

template <class T> struct Box46
{
    T m_Value;
    T m_Other;
    Box46() : m_Value(), m_Other() {}
    explicit Box46( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box46 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box46<int> IntBox46;
typedef Box46<double> DoubleBox46;
inline int Total46( const IntBox46 & a, const DoubleBox46 & b, const Record46 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 46 ) + (int)r.Mix( 46u );
}
static const int g_Table46[] = { 46, 92, 139, 232, 325, 510 };

struct Record47
{
    int m_Id47;
    double m_Value47;
    char m_Name47[ 20 ];
    Record47 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record47() : m_Id47( 47 ), m_Value47( 47.5 ), m_Next( nullptr ), m_Label( "record47" ), m_Flags3( 141u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name47 ); ++c ) { m_Name47[ c ] = (char)( 65 + ( ( c + 47 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id47 + a * 6 + b - (int)( m_Value47 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 78u ) ^ ( m_Flags3 << 8 ) ^ (unsigned)m_Id47; }
    bool Less( const Record47 & other ) const { return m_Id47 < other.m_Id47 || ( m_Id47 == other.m_Id47 && m_Value47 < other.m_Value47 ); }
};

template <class T> struct Box47
{
    T m_Value;
    T m_Other;
    Box47() : m_Value(), m_Other() {}
    explicit Box47( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box47 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box47<int> IntBox47;
typedef Box47<double> DoubleBox47;
inline int Total47( const IntBox47 & a, const DoubleBox47 & b, const Record47 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 47 ) + (int)r.Mix( 47u );
}
static const int g_Table47[] = { 47, 94, 142, 237, 332, 521 };

struct Record48
{
    int m_Id48;
    double m_Value48;
    char m_Name48[ 24 ];
    Record48 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record48() : m_Id48( 48 ), m_Value48( 48.5 ), m_Next( nullptr ), m_Label( "record48" ), m_Flags4( 144u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name48 ); ++c ) { m_Name48[ c ] = (char)( 65 + ( ( c + 48 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id48 + a * 7 + b - (int)( m_Value48 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 79u ) ^ ( m_Flags4 << 9 ) ^ (unsigned)m_Id48; }
    bool Less( const Record48 & other ) const { return m_Id48 < other.m_Id48 || ( m_Id48 == other.m_Id48 && m_Value48 < other.m_Value48 ); }
};

template <class T> struct Box48
{
    T m_Value;
    T m_Other;
    Box48() : m_Value(), m_Other() {}
    explicit Box48( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box48 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box48<int> IntBox48;
typedef Box48<double> DoubleBox48;
inline int Total48( const IntBox48 & a, const DoubleBox48 & b, const Record48 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 48 ) + (int)r.Mix( 48u );
}
static const int g_Table48[] = { 48, 96, 145, 242, 339, 532 };

struct Record49
{
    int m_Id49;
    double m_Value49;
    char m_Name49[ 28 ];
    Record49 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record49() : m_Id49( 49 ), m_Value49( 49.5 ), m_Next( nullptr ), m_Label( "record49" ), m_Flags5( 147u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name49 ); ++c ) { m_Name49[ c ] = (char)( 65 + ( ( c + 49 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id49 + a * 1 + b - (int)( m_Value49 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 80u ) ^ ( m_Flags5 << 10 ) ^ (unsigned)m_Id49; }
    bool Less( const Record49 & other ) const { return m_Id49 < other.m_Id49 || ( m_Id49 == other.m_Id49 && m_Value49 < other.m_Value49 ); }
};

template <class T> struct Box49
{
    T m_Value;
    T m_Other;
    Box49() : m_Value(), m_Other() {}
    explicit Box49( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box49 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box49<int> IntBox49;
typedef Box49<double> DoubleBox49;
inline int Total49( const IntBox49 & a, const DoubleBox49 & b, const Record49 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 49 ) + (int)r.Mix( 49u );
}
static const int g_Table49[] = { 49, 98, 148, 247, 346, 543 };

struct Record50
{
    int m_Id50;
    double m_Value50;
    char m_Name50[ 12 ];
    Record50 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record50() : m_Id50( 50 ), m_Value50( 50.5 ), m_Next( nullptr ), m_Label( "record50" ), m_Flags6( 150u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name50 ); ++c ) { m_Name50[ c ] = (char)( 65 + ( ( c + 50 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id50 + a * 2 + b - (int)( m_Value50 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 81u ) ^ ( m_Flags6 << 11 ) ^ (unsigned)m_Id50; }
    bool Less( const Record50 & other ) const { return m_Id50 < other.m_Id50 || ( m_Id50 == other.m_Id50 && m_Value50 < other.m_Value50 ); }
};

template <class T> struct Box50
{
    T m_Value;
    T m_Other;
    Box50() : m_Value(), m_Other() {}
    explicit Box50( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box50 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box50<int> IntBox50;
typedef Box50<double> DoubleBox50;
inline int Total50( const IntBox50 & a, const DoubleBox50 & b, const Record50 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 50 ) + (int)r.Mix( 50u );
}
static const int g_Table50[] = { 50, 100, 151, 252, 353, 554 };

struct Record51
{
    int m_Id51;
    double m_Value51;
    char m_Name51[ 16 ];
    Record51 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record51() : m_Id51( 51 ), m_Value51( 51.5 ), m_Next( nullptr ), m_Label( "record51" ), m_Flags7( 153u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name51 ); ++c ) { m_Name51[ c ] = (char)( 65 + ( ( c + 51 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id51 + a * 3 + b - (int)( m_Value51 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 82u ) ^ ( m_Flags7 << 12 ) ^ (unsigned)m_Id51; }
    bool Less( const Record51 & other ) const { return m_Id51 < other.m_Id51 || ( m_Id51 == other.m_Id51 && m_Value51 < other.m_Value51 ); }
};

template <class T> struct Box51
{
    T m_Value;
    T m_Other;
    Box51() : m_Value(), m_Other() {}
    explicit Box51( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box51 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box51<int> IntBox51;
typedef Box51<double> DoubleBox51;
inline int Total51( const IntBox51 & a, const DoubleBox51 & b, const Record51 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 51 ) + (int)r.Mix( 51u );
}
static const int g_Table51[] = { 51, 102, 154, 257, 360, 565 };

struct Record52
{
    int m_Id52;
    double m_Value52;
    char m_Name52[ 20 ];
    Record52 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record52() : m_Id52( 52 ), m_Value52( 52.5 ), m_Next( nullptr ), m_Label( "record52" ), m_Flags8( 156u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name52 ); ++c ) { m_Name52[ c ] = (char)( 65 + ( ( c + 52 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id52 + a * 4 + b - (int)( m_Value52 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 83u ) ^ ( m_Flags8 << 0 ) ^ (unsigned)m_Id52; }
    bool Less( const Record52 & other ) const { return m_Id52 < other.m_Id52 || ( m_Id52 == other.m_Id52 && m_Value52 < other.m_Value52 ); }
};

template <class T> struct Box52
{
    T m_Value;
    T m_Other;
    Box52() : m_Value(), m_Other() {}
    explicit Box52( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box52 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box52<int> IntBox52;
typedef Box52<double> DoubleBox52;
inline int Total52( const IntBox52 & a, const DoubleBox52 & b, const Record52 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 52 ) + (int)r.Mix( 52u );
}
static const int g_Table52[] = { 52, 104, 157, 262, 367, 576 };

struct Record53
{
    int m_Id53;
    double m_Value53;
    char m_Name53[ 24 ];
    Record53 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record53() : m_Id53( 53 ), m_Value53( 53.5 ), m_Next( nullptr ), m_Label( "record53" ), m_Flags9( 159u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name53 ); ++c ) { m_Name53[ c ] = (char)( 65 + ( ( c + 53 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id53 + a * 5 + b - (int)( m_Value53 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 84u ) ^ ( m_Flags9 << 1 ) ^ (unsigned)m_Id53; }
    bool Less( const Record53 & other ) const { return m_Id53 < other.m_Id53 || ( m_Id53 == other.m_Id53 && m_Value53 < other.m_Value53 ); }
};

template <class T> struct Box53
{
    T m_Value;
    T m_Other;
    Box53() : m_Value(), m_Other() {}
    explicit Box53( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box53 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box53<int> IntBox53;
typedef Box53<double> DoubleBox53;
inline int Total53( const IntBox53 & a, const DoubleBox53 & b, const Record53 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 53 ) + (int)r.Mix( 53u );
}
static const int g_Table53[] = { 53, 106, 160, 267, 374, 587 };

struct Record54
{
    int m_Id54;
    double m_Value54;
    char m_Name54[ 28 ];
    Record54 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record54() : m_Id54( 54 ), m_Value54( 54.5 ), m_Next( nullptr ), m_Label( "record54" ), m_Flags10( 162u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name54 ); ++c ) { m_Name54[ c ] = (char)( 65 + ( ( c + 54 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id54 + a * 6 + b - (int)( m_Value54 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 85u ) ^ ( m_Flags10 << 2 ) ^ (unsigned)m_Id54; }
    bool Less( const Record54 & other ) const { return m_Id54 < other.m_Id54 || ( m_Id54 == other.m_Id54 && m_Value54 < other.m_Value54 ); }
};

template <class T> struct Box54
{
    T m_Value;
    T m_Other;
    Box54() : m_Value(), m_Other() {}
    explicit Box54( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box54 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box54<int> IntBox54;
typedef Box54<double> DoubleBox54;
inline int Total54( const IntBox54 & a, const DoubleBox54 & b, const Record54 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 54 ) + (int)r.Mix( 54u );
}
static const int g_Table54[] = { 54, 108, 163, 272, 381, 598 };

struct Record55
{
    int m_Id55;
    double m_Value55;
    char m_Name55[ 12 ];
    Record55 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record55() : m_Id55( 55 ), m_Value55( 55.5 ), m_Next( nullptr ), m_Label( "record55" ), m_Flags0( 165u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name55 ); ++c ) { m_Name55[ c ] = (char)( 65 + ( ( c + 55 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id55 + a * 7 + b - (int)( m_Value55 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 86u ) ^ ( m_Flags0 << 3 ) ^ (unsigned)m_Id55; }
    bool Less( const Record55 & other ) const { return m_Id55 < other.m_Id55 || ( m_Id55 == other.m_Id55 && m_Value55 < other.m_Value55 ); }
};

template <class T> struct Box55
{
    T m_Value;
    T m_Other;
    Box55() : m_Value(), m_Other() {}
    explicit Box55( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box55 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box55<int> IntBox55;
typedef Box55<double> DoubleBox55;
inline int Total55( const IntBox55 & a, const DoubleBox55 & b, const Record55 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 55 ) + (int)r.Mix( 55u );
}
static const int g_Table55[] = { 55, 110, 166, 277, 388, 609 };

struct Record56
{
    int m_Id56;
    double m_Value56;
    char m_Name56[ 16 ];
    Record56 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record56() : m_Id56( 56 ), m_Value56( 56.5 ), m_Next( nullptr ), m_Label( "record56" ), m_Flags1( 168u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name56 ); ++c ) { m_Name56[ c ] = (char)( 65 + ( ( c + 56 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id56 + a * 1 + b - (int)( m_Value56 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 87u ) ^ ( m_Flags1 << 4 ) ^ (unsigned)m_Id56; }
    bool Less( const Record56 & other ) const { return m_Id56 < other.m_Id56 || ( m_Id56 == other.m_Id56 && m_Value56 < other.m_Value56 ); }
};

template <class T> struct Box56
{
    T m_Value;
    T m_Other;
    Box56() : m_Value(), m_Other() {}
    explicit Box56( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box56 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box56<int> IntBox56;
typedef Box56<double> DoubleBox56;
inline int Total56( const IntBox56 & a, const DoubleBox56 & b, const Record56 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 56 ) + (int)r.Mix( 56u );
}
static const int g_Table56[] = { 56, 112, 169, 282, 395, 620 };

struct Record57
{
    int m_Id57;
    double m_Value57;
    char m_Name57[ 20 ];
    Record57 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record57() : m_Id57( 57 ), m_Value57( 57.5 ), m_Next( nullptr ), m_Label( "record57" ), m_Flags2( 171u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name57 ); ++c ) { m_Name57[ c ] = (char)( 65 + ( ( c + 57 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id57 + a * 2 + b - (int)( m_Value57 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 88u ) ^ ( m_Flags2 << 5 ) ^ (unsigned)m_Id57; }
    bool Less( const Record57 & other ) const { return m_Id57 < other.m_Id57 || ( m_Id57 == other.m_Id57 && m_Value57 < other.m_Value57 ); }
};

template <class T> struct Box57
{
    T m_Value;
    T m_Other;
    Box57() : m_Value(), m_Other() {}
    explicit Box57( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box57 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box57<int> IntBox57;
typedef Box57<double> DoubleBox57;
inline int Total57( const IntBox57 & a, const DoubleBox57 & b, const Record57 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 57 ) + (int)r.Mix( 57u );
}
static const int g_Table57[] = { 57, 114, 172, 287, 402, 631 };

struct Record58
{
    int m_Id58;
    double m_Value58;
    char m_Name58[ 24 ];
    Record58 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record58() : m_Id58( 58 ), m_Value58( 58.5 ), m_Next( nullptr ), m_Label( "record58" ), m_Flags3( 174u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name58 ); ++c ) { m_Name58[ c ] = (char)( 65 + ( ( c + 58 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id58 + a * 3 + b - (int)( m_Value58 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 89u ) ^ ( m_Flags3 << 6 ) ^ (unsigned)m_Id58; }
    bool Less( const Record58 & other ) const { return m_Id58 < other.m_Id58 || ( m_Id58 == other.m_Id58 && m_Value58 < other.m_Value58 ); }
};

template <class T> struct Box58
{
    T m_Value;
    T m_Other;
    Box58() : m_Value(), m_Other() {}
    explicit Box58( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box58 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box58<int> IntBox58;
typedef Box58<double> DoubleBox58;
inline int Total58( const IntBox58 & a, const DoubleBox58 & b, const Record58 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 58 ) + (int)r.Mix( 58u );
}
static const int g_Table58[] = { 58, 116, 175, 292, 409, 642 };

struct Record59
{
    int m_Id59;
    double m_Value59;
    char m_Name59[ 28 ];
    Record59 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record59() : m_Id59( 59 ), m_Value59( 59.5 ), m_Next( nullptr ), m_Label( "record59" ), m_Flags4( 177u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name59 ); ++c ) { m_Name59[ c ] = (char)( 65 + ( ( c + 59 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id59 + a * 4 + b - (int)( m_Value59 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 90u ) ^ ( m_Flags4 << 7 ) ^ (unsigned)m_Id59; }
    bool Less( const Record59 & other ) const { return m_Id59 < other.m_Id59 || ( m_Id59 == other.m_Id59 && m_Value59 < other.m_Value59 ); }
};

template <class T> struct Box59
{
    T m_Value;
    T m_Other;
    Box59() : m_Value(), m_Other() {}
    explicit Box59( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box59 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box59<int> IntBox59;
typedef Box59<double> DoubleBox59;
inline int Total59( const IntBox59 & a, const DoubleBox59 & b, const Record59 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 59 ) + (int)r.Mix( 59u );
}
static const int g_Table59[] = { 59, 118, 178, 297, 416, 653 };

struct Record60
{
    int m_Id60;
    double m_Value60;
    char m_Name60[ 12 ];
    Record60 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record60() : m_Id60( 60 ), m_Value60( 60.5 ), m_Next( nullptr ), m_Label( "record60" ), m_Flags5( 180u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name60 ); ++c ) { m_Name60[ c ] = (char)( 65 + ( ( c + 60 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id60 + a * 5 + b - (int)( m_Value60 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 91u ) ^ ( m_Flags5 << 8 ) ^ (unsigned)m_Id60; }
    bool Less( const Record60 & other ) const { return m_Id60 < other.m_Id60 || ( m_Id60 == other.m_Id60 && m_Value60 < other.m_Value60 ); }
};

template <class T> struct Box60
{
    T m_Value;
    T m_Other;
    Box60() : m_Value(), m_Other() {}
    explicit Box60( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box60 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box60<int> IntBox60;
typedef Box60<double> DoubleBox60;
inline int Total60( const IntBox60 & a, const DoubleBox60 & b, const Record60 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 60 ) + (int)r.Mix( 60u );
}
static const int g_Table60[] = { 60, 120, 181, 302, 423, 664 };

struct Record61
{
    int m_Id61;
    double m_Value61;
    char m_Name61[ 16 ];
    Record61 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record61() : m_Id61( 61 ), m_Value61( 61.5 ), m_Next( nullptr ), m_Label( "record61" ), m_Flags6( 183u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name61 ); ++c ) { m_Name61[ c ] = (char)( 65 + ( ( c + 61 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id61 + a * 6 + b - (int)( m_Value61 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 92u ) ^ ( m_Flags6 << 9 ) ^ (unsigned)m_Id61; }
    bool Less( const Record61 & other ) const { return m_Id61 < other.m_Id61 || ( m_Id61 == other.m_Id61 && m_Value61 < other.m_Value61 ); }
};

template <class T> struct Box61
{
    T m_Value;
    T m_Other;
    Box61() : m_Value(), m_Other() {}
    explicit Box61( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box61 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box61<int> IntBox61;
typedef Box61<double> DoubleBox61;
inline int Total61( const IntBox61 & a, const DoubleBox61 & b, const Record61 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 61 ) + (int)r.Mix( 61u );
}
static const int g_Table61[] = { 61, 122, 184, 307, 430, 675 };

struct Record62
{
    int m_Id62;
    double m_Value62;
    char m_Name62[ 20 ];
    Record62 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record62() : m_Id62( 62 ), m_Value62( 62.5 ), m_Next( nullptr ), m_Label( "record62" ), m_Flags7( 186u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name62 ); ++c ) { m_Name62[ c ] = (char)( 65 + ( ( c + 62 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id62 + a * 7 + b - (int)( m_Value62 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 93u ) ^ ( m_Flags7 << 10 ) ^ (unsigned)m_Id62; }
    bool Less( const Record62 & other ) const { return m_Id62 < other.m_Id62 || ( m_Id62 == other.m_Id62 && m_Value62 < other.m_Value62 ); }
};

template <class T> struct Box62
{
    T m_Value;
    T m_Other;
    Box62() : m_Value(), m_Other() {}
    explicit Box62( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box62 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box62<int> IntBox62;
typedef Box62<double> DoubleBox62;
inline int Total62( const IntBox62 & a, const DoubleBox62 & b, const Record62 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 62 ) + (int)r.Mix( 62u );
}
static const int g_Table62[] = { 62, 124, 187, 312, 437, 686 };

struct Record63
{
    int m_Id63;
    double m_Value63;
    char m_Name63[ 24 ];
    Record63 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record63() : m_Id63( 63 ), m_Value63( 63.5 ), m_Next( nullptr ), m_Label( "record63" ), m_Flags8( 189u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name63 ); ++c ) { m_Name63[ c ] = (char)( 65 + ( ( c + 63 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id63 + a * 1 + b - (int)( m_Value63 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 94u ) ^ ( m_Flags8 << 11 ) ^ (unsigned)m_Id63; }
    bool Less( const Record63 & other ) const { return m_Id63 < other.m_Id63 || ( m_Id63 == other.m_Id63 && m_Value63 < other.m_Value63 ); }
};

template <class T> struct Box63
{
    T m_Value;
    T m_Other;
    Box63() : m_Value(), m_Other() {}
    explicit Box63( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box63 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box63<int> IntBox63;
typedef Box63<double> DoubleBox63;
inline int Total63( const IntBox63 & a, const DoubleBox63 & b, const Record63 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 63 ) + (int)r.Mix( 63u );
}
static const int g_Table63[] = { 63, 126, 190, 317, 444, 697 };

struct Record64
{
    int m_Id64;
    double m_Value64;
    char m_Name64[ 28 ];
    Record64 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record64() : m_Id64( 64 ), m_Value64( 64.5 ), m_Next( nullptr ), m_Label( "record64" ), m_Flags9( 192u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name64 ); ++c ) { m_Name64[ c ] = (char)( 65 + ( ( c + 64 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id64 + a * 2 + b - (int)( m_Value64 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 95u ) ^ ( m_Flags9 << 12 ) ^ (unsigned)m_Id64; }
    bool Less( const Record64 & other ) const { return m_Id64 < other.m_Id64 || ( m_Id64 == other.m_Id64 && m_Value64 < other.m_Value64 ); }
};

template <class T> struct Box64
{
    T m_Value;
    T m_Other;
    Box64() : m_Value(), m_Other() {}
    explicit Box64( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box64 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box64<int> IntBox64;
typedef Box64<double> DoubleBox64;
inline int Total64( const IntBox64 & a, const DoubleBox64 & b, const Record64 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 64 ) + (int)r.Mix( 64u );
}
static const int g_Table64[] = { 64, 128, 193, 322, 451, 708 };

struct Record65
{
    int m_Id65;
    double m_Value65;
    char m_Name65[ 12 ];
    Record65 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record65() : m_Id65( 65 ), m_Value65( 65.5 ), m_Next( nullptr ), m_Label( "record65" ), m_Flags10( 195u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name65 ); ++c ) { m_Name65[ c ] = (char)( 65 + ( ( c + 65 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id65 + a * 3 + b - (int)( m_Value65 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 96u ) ^ ( m_Flags10 << 0 ) ^ (unsigned)m_Id65; }
    bool Less( const Record65 & other ) const { return m_Id65 < other.m_Id65 || ( m_Id65 == other.m_Id65 && m_Value65 < other.m_Value65 ); }
};

template <class T> struct Box65
{
    T m_Value;
    T m_Other;
    Box65() : m_Value(), m_Other() {}
    explicit Box65( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box65 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box65<int> IntBox65;
typedef Box65<double> DoubleBox65;
inline int Total65( const IntBox65 & a, const DoubleBox65 & b, const Record65 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 65 ) + (int)r.Mix( 65u );
}
static const int g_Table65[] = { 65, 130, 196, 327, 458, 719 };

struct Record66
{
    int m_Id66;
    double m_Value66;
    char m_Name66[ 16 ];
    Record66 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record66() : m_Id66( 66 ), m_Value66( 66.5 ), m_Next( nullptr ), m_Label( "record66" ), m_Flags0( 198u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name66 ); ++c ) { m_Name66[ c ] = (char)( 65 + ( ( c + 66 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id66 + a * 4 + b - (int)( m_Value66 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 97u ) ^ ( m_Flags0 << 1 ) ^ (unsigned)m_Id66; }
    bool Less( const Record66 & other ) const { return m_Id66 < other.m_Id66 || ( m_Id66 == other.m_Id66 && m_Value66 < other.m_Value66 ); }
};

template <class T> struct Box66
{
    T m_Value;
    T m_Other;
    Box66() : m_Value(), m_Other() {}
    explicit Box66( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box66 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box66<int> IntBox66;
typedef Box66<double> DoubleBox66;
inline int Total66( const IntBox66 & a, const DoubleBox66 & b, const Record66 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 66 ) + (int)r.Mix( 66u );
}
static const int g_Table66[] = { 66, 132, 199, 332, 465, 730 };

struct Record67
{
    int m_Id67;
    double m_Value67;
    char m_Name67[ 20 ];
    Record67 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record67() : m_Id67( 67 ), m_Value67( 67.5 ), m_Next( nullptr ), m_Label( "record67" ), m_Flags1( 201u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name67 ); ++c ) { m_Name67[ c ] = (char)( 65 + ( ( c + 67 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id67 + a * 5 + b - (int)( m_Value67 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 98u ) ^ ( m_Flags1 << 2 ) ^ (unsigned)m_Id67; }
    bool Less( const Record67 & other ) const { return m_Id67 < other.m_Id67 || ( m_Id67 == other.m_Id67 && m_Value67 < other.m_Value67 ); }
};

template <class T> struct Box67
{
    T m_Value;
    T m_Other;
    Box67() : m_Value(), m_Other() {}
    explicit Box67( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box67 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box67<int> IntBox67;
typedef Box67<double> DoubleBox67;
inline int Total67( const IntBox67 & a, const DoubleBox67 & b, const Record67 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 67 ) + (int)r.Mix( 67u );
}
static const int g_Table67[] = { 67, 134, 202, 337, 472, 741 };

struct Record68
{
    int m_Id68;
    double m_Value68;
    char m_Name68[ 24 ];
    Record68 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record68() : m_Id68( 68 ), m_Value68( 68.5 ), m_Next( nullptr ), m_Label( "record68" ), m_Flags2( 204u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name68 ); ++c ) { m_Name68[ c ] = (char)( 65 + ( ( c + 68 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id68 + a * 6 + b - (int)( m_Value68 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 99u ) ^ ( m_Flags2 << 3 ) ^ (unsigned)m_Id68; }
    bool Less( const Record68 & other ) const { return m_Id68 < other.m_Id68 || ( m_Id68 == other.m_Id68 && m_Value68 < other.m_Value68 ); }
};

template <class T> struct Box68
{
    T m_Value;
    T m_Other;
    Box68() : m_Value(), m_Other() {}
    explicit Box68( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box68 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box68<int> IntBox68;
typedef Box68<double> DoubleBox68;
inline int Total68( const IntBox68 & a, const DoubleBox68 & b, const Record68 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 68 ) + (int)r.Mix( 68u );
}
static const int g_Table68[] = { 68, 136, 205, 342, 479, 752 };

struct Record69
{
    int m_Id69;
    double m_Value69;
    char m_Name69[ 28 ];
    Record69 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record69() : m_Id69( 69 ), m_Value69( 69.5 ), m_Next( nullptr ), m_Label( "record69" ), m_Flags3( 207u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name69 ); ++c ) { m_Name69[ c ] = (char)( 65 + ( ( c + 69 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id69 + a * 7 + b - (int)( m_Value69 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 100u ) ^ ( m_Flags3 << 4 ) ^ (unsigned)m_Id69; }
    bool Less( const Record69 & other ) const { return m_Id69 < other.m_Id69 || ( m_Id69 == other.m_Id69 && m_Value69 < other.m_Value69 ); }
};

template <class T> struct Box69
{
    T m_Value;
    T m_Other;
    Box69() : m_Value(), m_Other() {}
    explicit Box69( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box69 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box69<int> IntBox69;
typedef Box69<double> DoubleBox69;
inline int Total69( const IntBox69 & a, const DoubleBox69 & b, const Record69 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 69 ) + (int)r.Mix( 69u );
}
static const int g_Table69[] = { 69, 138, 208, 347, 486, 763 };

struct Record70
{
    int m_Id70;
    double m_Value70;
    char m_Name70[ 12 ];
    Record70 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record70() : m_Id70( 70 ), m_Value70( 70.5 ), m_Next( nullptr ), m_Label( "record70" ), m_Flags4( 210u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name70 ); ++c ) { m_Name70[ c ] = (char)( 65 + ( ( c + 70 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id70 + a * 1 + b - (int)( m_Value70 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 101u ) ^ ( m_Flags4 << 5 ) ^ (unsigned)m_Id70; }
    bool Less( const Record70 & other ) const { return m_Id70 < other.m_Id70 || ( m_Id70 == other.m_Id70 && m_Value70 < other.m_Value70 ); }
};

template <class T> struct Box70
{
    T m_Value;
    T m_Other;
    Box70() : m_Value(), m_Other() {}
    explicit Box70( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box70 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box70<int> IntBox70;
typedef Box70<double> DoubleBox70;
inline int Total70( const IntBox70 & a, const DoubleBox70 & b, const Record70 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 70 ) + (int)r.Mix( 70u );
}
static const int g_Table70[] = { 70, 140, 211, 352, 493, 774 };

struct Record71
{
    int m_Id71;
    double m_Value71;
    char m_Name71[ 16 ];
    Record71 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record71() : m_Id71( 71 ), m_Value71( 71.5 ), m_Next( nullptr ), m_Label( "record71" ), m_Flags5( 213u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name71 ); ++c ) { m_Name71[ c ] = (char)( 65 + ( ( c + 71 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id71 + a * 2 + b - (int)( m_Value71 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 102u ) ^ ( m_Flags5 << 6 ) ^ (unsigned)m_Id71; }
    bool Less( const Record71 & other ) const { return m_Id71 < other.m_Id71 || ( m_Id71 == other.m_Id71 && m_Value71 < other.m_Value71 ); }
};

template <class T> struct Box71
{
    T m_Value;
    T m_Other;
    Box71() : m_Value(), m_Other() {}
    explicit Box71( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box71 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box71<int> IntBox71;
typedef Box71<double> DoubleBox71;
inline int Total71( const IntBox71 & a, const DoubleBox71 & b, const Record71 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 71 ) + (int)r.Mix( 71u );
}
static const int g_Table71[] = { 71, 142, 214, 357, 500, 785 };

struct Record72
{
    int m_Id72;
    double m_Value72;
    char m_Name72[ 20 ];
    Record72 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record72() : m_Id72( 72 ), m_Value72( 72.5 ), m_Next( nullptr ), m_Label( "record72" ), m_Flags6( 216u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name72 ); ++c ) { m_Name72[ c ] = (char)( 65 + ( ( c + 72 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id72 + a * 3 + b - (int)( m_Value72 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 103u ) ^ ( m_Flags6 << 7 ) ^ (unsigned)m_Id72; }
    bool Less( const Record72 & other ) const { return m_Id72 < other.m_Id72 || ( m_Id72 == other.m_Id72 && m_Value72 < other.m_Value72 ); }
};

template <class T> struct Box72
{
    T m_Value;
    T m_Other;
    Box72() : m_Value(), m_Other() {}
    explicit Box72( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box72 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box72<int> IntBox72;
typedef Box72<double> DoubleBox72;
inline int Total72( const IntBox72 & a, const DoubleBox72 & b, const Record72 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 72 ) + (int)r.Mix( 72u );
}
static const int g_Table72[] = { 72, 144, 217, 362, 507, 796 };

struct Record73
{
    int m_Id73;
    double m_Value73;
    char m_Name73[ 24 ];
    Record73 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record73() : m_Id73( 73 ), m_Value73( 73.5 ), m_Next( nullptr ), m_Label( "record73" ), m_Flags7( 219u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name73 ); ++c ) { m_Name73[ c ] = (char)( 65 + ( ( c + 73 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id73 + a * 4 + b - (int)( m_Value73 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 104u ) ^ ( m_Flags7 << 8 ) ^ (unsigned)m_Id73; }
    bool Less( const Record73 & other ) const { return m_Id73 < other.m_Id73 || ( m_Id73 == other.m_Id73 && m_Value73 < other.m_Value73 ); }
};

template <class T> struct Box73
{
    T m_Value;
    T m_Other;
    Box73() : m_Value(), m_Other() {}
    explicit Box73( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box73 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box73<int> IntBox73;
typedef Box73<double> DoubleBox73;
inline int Total73( const IntBox73 & a, const DoubleBox73 & b, const Record73 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 73 ) + (int)r.Mix( 73u );
}
static const int g_Table73[] = { 73, 146, 220, 367, 514, 807 };

struct Record74
{
    int m_Id74;
    double m_Value74;
    char m_Name74[ 28 ];
    Record74 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record74() : m_Id74( 74 ), m_Value74( 74.5 ), m_Next( nullptr ), m_Label( "record74" ), m_Flags8( 222u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name74 ); ++c ) { m_Name74[ c ] = (char)( 65 + ( ( c + 74 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id74 + a * 5 + b - (int)( m_Value74 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 105u ) ^ ( m_Flags8 << 9 ) ^ (unsigned)m_Id74; }
    bool Less( const Record74 & other ) const { return m_Id74 < other.m_Id74 || ( m_Id74 == other.m_Id74 && m_Value74 < other.m_Value74 ); }
};

template <class T> struct Box74
{
    T m_Value;
    T m_Other;
    Box74() : m_Value(), m_Other() {}
    explicit Box74( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box74 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box74<int> IntBox74;
typedef Box74<double> DoubleBox74;
inline int Total74( const IntBox74 & a, const DoubleBox74 & b, const Record74 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 74 ) + (int)r.Mix( 74u );
}
static const int g_Table74[] = { 74, 148, 223, 372, 521, 818 };

struct Record75
{
    int m_Id75;
    double m_Value75;
    char m_Name75[ 12 ];
    Record75 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record75() : m_Id75( 75 ), m_Value75( 75.5 ), m_Next( nullptr ), m_Label( "record75" ), m_Flags9( 225u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name75 ); ++c ) { m_Name75[ c ] = (char)( 65 + ( ( c + 75 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id75 + a * 6 + b - (int)( m_Value75 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 106u ) ^ ( m_Flags9 << 10 ) ^ (unsigned)m_Id75; }
    bool Less( const Record75 & other ) const { return m_Id75 < other.m_Id75 || ( m_Id75 == other.m_Id75 && m_Value75 < other.m_Value75 ); }
};

template <class T> struct Box75
{
    T m_Value;
    T m_Other;
    Box75() : m_Value(), m_Other() {}
    explicit Box75( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box75 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box75<int> IntBox75;
typedef Box75<double> DoubleBox75;
inline int Total75( const IntBox75 & a, const DoubleBox75 & b, const Record75 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 75 ) + (int)r.Mix( 75u );
}
static const int g_Table75[] = { 75, 150, 226, 377, 528, 829 };

struct Record76
{
    int m_Id76;
    double m_Value76;
    char m_Name76[ 16 ];
    Record76 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record76() : m_Id76( 76 ), m_Value76( 76.5 ), m_Next( nullptr ), m_Label( "record76" ), m_Flags10( 228u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name76 ); ++c ) { m_Name76[ c ] = (char)( 65 + ( ( c + 76 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id76 + a * 7 + b - (int)( m_Value76 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 107u ) ^ ( m_Flags10 << 11 ) ^ (unsigned)m_Id76; }
    bool Less( const Record76 & other ) const { return m_Id76 < other.m_Id76 || ( m_Id76 == other.m_Id76 && m_Value76 < other.m_Value76 ); }
};

template <class T> struct Box76
{
    T m_Value;
    T m_Other;
    Box76() : m_Value(), m_Other() {}
    explicit Box76( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box76 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box76<int> IntBox76;
typedef Box76<double> DoubleBox76;
inline int Total76( const IntBox76 & a, const DoubleBox76 & b, const Record76 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 76 ) + (int)r.Mix( 76u );
}
static const int g_Table76[] = { 76, 152, 229, 382, 535, 840 };

struct Record77
{
    int m_Id77;
    double m_Value77;
    char m_Name77[ 20 ];
    Record77 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record77() : m_Id77( 77 ), m_Value77( 77.5 ), m_Next( nullptr ), m_Label( "record77" ), m_Flags0( 231u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name77 ); ++c ) { m_Name77[ c ] = (char)( 65 + ( ( c + 77 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id77 + a * 1 + b - (int)( m_Value77 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 108u ) ^ ( m_Flags0 << 12 ) ^ (unsigned)m_Id77; }
    bool Less( const Record77 & other ) const { return m_Id77 < other.m_Id77 || ( m_Id77 == other.m_Id77 && m_Value77 < other.m_Value77 ); }
};

template <class T> struct Box77
{
    T m_Value;
    T m_Other;
    Box77() : m_Value(), m_Other() {}
    explicit Box77( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box77 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box77<int> IntBox77;
typedef Box77<double> DoubleBox77;
inline int Total77( const IntBox77 & a, const DoubleBox77 & b, const Record77 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 77 ) + (int)r.Mix( 77u );
}
static const int g_Table77[] = { 77, 154, 232, 387, 542, 851 };

struct Record78
{
    int m_Id78;
    double m_Value78;
    char m_Name78[ 24 ];
    Record78 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record78() : m_Id78( 78 ), m_Value78( 78.5 ), m_Next( nullptr ), m_Label( "record78" ), m_Flags1( 234u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name78 ); ++c ) { m_Name78[ c ] = (char)( 65 + ( ( c + 78 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id78 + a * 2 + b - (int)( m_Value78 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 109u ) ^ ( m_Flags1 << 0 ) ^ (unsigned)m_Id78; }
    bool Less( const Record78 & other ) const { return m_Id78 < other.m_Id78 || ( m_Id78 == other.m_Id78 && m_Value78 < other.m_Value78 ); }
};

template <class T> struct Box78
{
    T m_Value;
    T m_Other;
    Box78() : m_Value(), m_Other() {}
    explicit Box78( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box78 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box78<int> IntBox78;
typedef Box78<double> DoubleBox78;
inline int Total78( const IntBox78 & a, const DoubleBox78 & b, const Record78 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 78 ) + (int)r.Mix( 78u );
}
static const int g_Table78[] = { 78, 156, 235, 392, 549, 862 };

struct Record79
{
    int m_Id79;
    double m_Value79;
    char m_Name79[ 28 ];
    Record79 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record79() : m_Id79( 79 ), m_Value79( 79.5 ), m_Next( nullptr ), m_Label( "record79" ), m_Flags2( 237u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name79 ); ++c ) { m_Name79[ c ] = (char)( 65 + ( ( c + 79 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id79 + a * 3 + b - (int)( m_Value79 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 110u ) ^ ( m_Flags2 << 1 ) ^ (unsigned)m_Id79; }
    bool Less( const Record79 & other ) const { return m_Id79 < other.m_Id79 || ( m_Id79 == other.m_Id79 && m_Value79 < other.m_Value79 ); }
};

template <class T> struct Box79
{
    T m_Value;
    T m_Other;
    Box79() : m_Value(), m_Other() {}
    explicit Box79( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box79 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box79<int> IntBox79;
typedef Box79<double> DoubleBox79;
inline int Total79( const IntBox79 & a, const DoubleBox79 & b, const Record79 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 79 ) + (int)r.Mix( 79u );
}
static const int g_Table79[] = { 79, 158, 238, 397, 556, 873 };

struct Record80
{
    int m_Id80;
    double m_Value80;
    char m_Name80[ 12 ];
    Record80 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record80() : m_Id80( 80 ), m_Value80( 80.5 ), m_Next( nullptr ), m_Label( "record80" ), m_Flags3( 240u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name80 ); ++c ) { m_Name80[ c ] = (char)( 65 + ( ( c + 80 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id80 + a * 4 + b - (int)( m_Value80 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 111u ) ^ ( m_Flags3 << 2 ) ^ (unsigned)m_Id80; }
    bool Less( const Record80 & other ) const { return m_Id80 < other.m_Id80 || ( m_Id80 == other.m_Id80 && m_Value80 < other.m_Value80 ); }
};

template <class T> struct Box80
{
    T m_Value;
    T m_Other;
    Box80() : m_Value(), m_Other() {}
    explicit Box80( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box80 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box80<int> IntBox80;
typedef Box80<double> DoubleBox80;
inline int Total80( const IntBox80 & a, const DoubleBox80 & b, const Record80 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 80 ) + (int)r.Mix( 80u );
}
static const int g_Table80[] = { 80, 160, 241, 402, 563, 884 };

struct Record81
{
    int m_Id81;
    double m_Value81;
    char m_Name81[ 16 ];
    Record81 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record81() : m_Id81( 81 ), m_Value81( 81.5 ), m_Next( nullptr ), m_Label( "record81" ), m_Flags4( 243u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name81 ); ++c ) { m_Name81[ c ] = (char)( 65 + ( ( c + 81 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id81 + a * 5 + b - (int)( m_Value81 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 112u ) ^ ( m_Flags4 << 3 ) ^ (unsigned)m_Id81; }
    bool Less( const Record81 & other ) const { return m_Id81 < other.m_Id81 || ( m_Id81 == other.m_Id81 && m_Value81 < other.m_Value81 ); }
};

template <class T> struct Box81
{
    T m_Value;
    T m_Other;
    Box81() : m_Value(), m_Other() {}
    explicit Box81( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box81 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box81<int> IntBox81;
typedef Box81<double> DoubleBox81;
inline int Total81( const IntBox81 & a, const DoubleBox81 & b, const Record81 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 81 ) + (int)r.Mix( 81u );
}
static const int g_Table81[] = { 81, 162, 244, 407, 570, 895 };

struct Record82
{
    int m_Id82;
    double m_Value82;
    char m_Name82[ 20 ];
    Record82 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record82() : m_Id82( 82 ), m_Value82( 82.5 ), m_Next( nullptr ), m_Label( "record82" ), m_Flags5( 246u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name82 ); ++c ) { m_Name82[ c ] = (char)( 65 + ( ( c + 82 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id82 + a * 6 + b - (int)( m_Value82 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 113u ) ^ ( m_Flags5 << 4 ) ^ (unsigned)m_Id82; }
    bool Less( const Record82 & other ) const { return m_Id82 < other.m_Id82 || ( m_Id82 == other.m_Id82 && m_Value82 < other.m_Value82 ); }
};

template <class T> struct Box82
{
    T m_Value;
    T m_Other;
    Box82() : m_Value(), m_Other() {}
    explicit Box82( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box82 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box82<int> IntBox82;
typedef Box82<double> DoubleBox82;
inline int Total82( const IntBox82 & a, const DoubleBox82 & b, const Record82 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 82 ) + (int)r.Mix( 82u );
}
static const int g_Table82[] = { 82, 164, 247, 412, 577, 906 };

struct Record83
{
    int m_Id83;
    double m_Value83;
    char m_Name83[ 24 ];
    Record83 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record83() : m_Id83( 83 ), m_Value83( 83.5 ), m_Next( nullptr ), m_Label( "record83" ), m_Flags6( 249u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name83 ); ++c ) { m_Name83[ c ] = (char)( 65 + ( ( c + 83 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id83 + a * 7 + b - (int)( m_Value83 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 114u ) ^ ( m_Flags6 << 5 ) ^ (unsigned)m_Id83; }
    bool Less( const Record83 & other ) const { return m_Id83 < other.m_Id83 || ( m_Id83 == other.m_Id83 && m_Value83 < other.m_Value83 ); }
};

template <class T> struct Box83
{
    T m_Value;
    T m_Other;
    Box83() : m_Value(), m_Other() {}
    explicit Box83( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box83 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box83<int> IntBox83;
typedef Box83<double> DoubleBox83;
inline int Total83( const IntBox83 & a, const DoubleBox83 & b, const Record83 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 83 ) + (int)r.Mix( 83u );
}
static const int g_Table83[] = { 83, 166, 250, 417, 584, 917 };

struct Record84
{
    int m_Id84;
    double m_Value84;
    char m_Name84[ 28 ];
    Record84 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record84() : m_Id84( 84 ), m_Value84( 84.5 ), m_Next( nullptr ), m_Label( "record84" ), m_Flags7( 252u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name84 ); ++c ) { m_Name84[ c ] = (char)( 65 + ( ( c + 84 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id84 + a * 1 + b - (int)( m_Value84 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 115u ) ^ ( m_Flags7 << 6 ) ^ (unsigned)m_Id84; }
    bool Less( const Record84 & other ) const { return m_Id84 < other.m_Id84 || ( m_Id84 == other.m_Id84 && m_Value84 < other.m_Value84 ); }
};

template <class T> struct Box84
{
    T m_Value;
    T m_Other;
    Box84() : m_Value(), m_Other() {}
    explicit Box84( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box84 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box84<int> IntBox84;
typedef Box84<double> DoubleBox84;
inline int Total84( const IntBox84 & a, const DoubleBox84 & b, const Record84 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 84 ) + (int)r.Mix( 84u );
}
static const int g_Table84[] = { 84, 168, 253, 422, 591, 928 };

struct Record85
{
    int m_Id85;
    double m_Value85;
    char m_Name85[ 12 ];
    Record85 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record85() : m_Id85( 85 ), m_Value85( 85.5 ), m_Next( nullptr ), m_Label( "record85" ), m_Flags8( 255u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name85 ); ++c ) { m_Name85[ c ] = (char)( 65 + ( ( c + 85 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id85 + a * 2 + b - (int)( m_Value85 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 116u ) ^ ( m_Flags8 << 7 ) ^ (unsigned)m_Id85; }
    bool Less( const Record85 & other ) const { return m_Id85 < other.m_Id85 || ( m_Id85 == other.m_Id85 && m_Value85 < other.m_Value85 ); }
};

template <class T> struct Box85
{
    T m_Value;
    T m_Other;
    Box85() : m_Value(), m_Other() {}
    explicit Box85( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box85 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box85<int> IntBox85;
typedef Box85<double> DoubleBox85;
inline int Total85( const IntBox85 & a, const DoubleBox85 & b, const Record85 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 85 ) + (int)r.Mix( 85u );
}
static const int g_Table85[] = { 85, 170, 256, 427, 598, 939 };

struct Record86
{
    int m_Id86;
    double m_Value86;
    char m_Name86[ 16 ];
    Record86 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record86() : m_Id86( 86 ), m_Value86( 86.5 ), m_Next( nullptr ), m_Label( "record86" ), m_Flags9( 258u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name86 ); ++c ) { m_Name86[ c ] = (char)( 65 + ( ( c + 86 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id86 + a * 3 + b - (int)( m_Value86 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 117u ) ^ ( m_Flags9 << 8 ) ^ (unsigned)m_Id86; }
    bool Less( const Record86 & other ) const { return m_Id86 < other.m_Id86 || ( m_Id86 == other.m_Id86 && m_Value86 < other.m_Value86 ); }
};

template <class T> struct Box86
{
    T m_Value;
    T m_Other;
    Box86() : m_Value(), m_Other() {}
    explicit Box86( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box86 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box86<int> IntBox86;
typedef Box86<double> DoubleBox86;
inline int Total86( const IntBox86 & a, const DoubleBox86 & b, const Record86 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 86 ) + (int)r.Mix( 86u );
}
static const int g_Table86[] = { 86, 172, 259, 432, 605, 950 };

struct Record87
{
    int m_Id87;
    double m_Value87;
    char m_Name87[ 20 ];
    Record87 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record87() : m_Id87( 87 ), m_Value87( 87.5 ), m_Next( nullptr ), m_Label( "record87" ), m_Flags10( 261u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name87 ); ++c ) { m_Name87[ c ] = (char)( 65 + ( ( c + 87 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id87 + a * 4 + b - (int)( m_Value87 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 118u ) ^ ( m_Flags10 << 9 ) ^ (unsigned)m_Id87; }
    bool Less( const Record87 & other ) const { return m_Id87 < other.m_Id87 || ( m_Id87 == other.m_Id87 && m_Value87 < other.m_Value87 ); }
};

template <class T> struct Box87
{
    T m_Value;
    T m_Other;
    Box87() : m_Value(), m_Other() {}
    explicit Box87( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box87 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box87<int> IntBox87;
typedef Box87<double> DoubleBox87;
inline int Total87( const IntBox87 & a, const DoubleBox87 & b, const Record87 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 87 ) + (int)r.Mix( 87u );
}
static const int g_Table87[] = { 87, 174, 262, 437, 612, 961 };

struct Record88
{
    int m_Id88;
    double m_Value88;
    char m_Name88[ 24 ];
    Record88 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record88() : m_Id88( 88 ), m_Value88( 88.5 ), m_Next( nullptr ), m_Label( "record88" ), m_Flags0( 264u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name88 ); ++c ) { m_Name88[ c ] = (char)( 65 + ( ( c + 88 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id88 + a * 5 + b - (int)( m_Value88 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 119u ) ^ ( m_Flags0 << 10 ) ^ (unsigned)m_Id88; }
    bool Less( const Record88 & other ) const { return m_Id88 < other.m_Id88 || ( m_Id88 == other.m_Id88 && m_Value88 < other.m_Value88 ); }
};

template <class T> struct Box88
{
    T m_Value;
    T m_Other;
    Box88() : m_Value(), m_Other() {}
    explicit Box88( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box88 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box88<int> IntBox88;
typedef Box88<double> DoubleBox88;
inline int Total88( const IntBox88 & a, const DoubleBox88 & b, const Record88 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 88 ) + (int)r.Mix( 88u );
}
static const int g_Table88[] = { 88, 176, 265, 442, 619, 972 };

struct Record89
{
    int m_Id89;
    double m_Value89;
    char m_Name89[ 28 ];
    Record89 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record89() : m_Id89( 89 ), m_Value89( 89.5 ), m_Next( nullptr ), m_Label( "record89" ), m_Flags1( 267u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name89 ); ++c ) { m_Name89[ c ] = (char)( 65 + ( ( c + 89 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id89 + a * 6 + b - (int)( m_Value89 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 120u ) ^ ( m_Flags1 << 11 ) ^ (unsigned)m_Id89; }
    bool Less( const Record89 & other ) const { return m_Id89 < other.m_Id89 || ( m_Id89 == other.m_Id89 && m_Value89 < other.m_Value89 ); }
};

template <class T> struct Box89
{
    T m_Value;
    T m_Other;
    Box89() : m_Value(), m_Other() {}
    explicit Box89( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box89 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box89<int> IntBox89;
typedef Box89<double> DoubleBox89;
inline int Total89( const IntBox89 & a, const DoubleBox89 & b, const Record89 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 89 ) + (int)r.Mix( 89u );
}
static const int g_Table89[] = { 89, 178, 268, 447, 626, 983 };

struct Record90
{
    int m_Id90;
    double m_Value90;
    char m_Name90[ 12 ];
    Record90 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record90() : m_Id90( 90 ), m_Value90( 90.5 ), m_Next( nullptr ), m_Label( "record90" ), m_Flags2( 270u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name90 ); ++c ) { m_Name90[ c ] = (char)( 65 + ( ( c + 90 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id90 + a * 7 + b - (int)( m_Value90 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 121u ) ^ ( m_Flags2 << 12 ) ^ (unsigned)m_Id90; }
    bool Less( const Record90 & other ) const { return m_Id90 < other.m_Id90 || ( m_Id90 == other.m_Id90 && m_Value90 < other.m_Value90 ); }
};

template <class T> struct Box90
{
    T m_Value;
    T m_Other;
    Box90() : m_Value(), m_Other() {}
    explicit Box90( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box90 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box90<int> IntBox90;
typedef Box90<double> DoubleBox90;
inline int Total90( const IntBox90 & a, const DoubleBox90 & b, const Record90 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 90 ) + (int)r.Mix( 90u );
}
static const int g_Table90[] = { 90, 180, 271, 452, 633, 994 };

struct Record91
{
    int m_Id91;
    double m_Value91;
    char m_Name91[ 16 ];
    Record91 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record91() : m_Id91( 91 ), m_Value91( 91.5 ), m_Next( nullptr ), m_Label( "record91" ), m_Flags3( 273u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name91 ); ++c ) { m_Name91[ c ] = (char)( 65 + ( ( c + 91 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id91 + a * 1 + b - (int)( m_Value91 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 122u ) ^ ( m_Flags3 << 0 ) ^ (unsigned)m_Id91; }
    bool Less( const Record91 & other ) const { return m_Id91 < other.m_Id91 || ( m_Id91 == other.m_Id91 && m_Value91 < other.m_Value91 ); }
};

template <class T> struct Box91
{
    T m_Value;
    T m_Other;
    Box91() : m_Value(), m_Other() {}
    explicit Box91( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box91 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box91<int> IntBox91;
typedef Box91<double> DoubleBox91;
inline int Total91( const IntBox91 & a, const DoubleBox91 & b, const Record91 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 91 ) + (int)r.Mix( 91u );
}
static const int g_Table91[] = { 91, 182, 274, 457, 640, 1005 };

struct Record92
{
    int m_Id92;
    double m_Value92;
    char m_Name92[ 20 ];
    Record92 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record92() : m_Id92( 92 ), m_Value92( 92.5 ), m_Next( nullptr ), m_Label( "record92" ), m_Flags4( 276u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name92 ); ++c ) { m_Name92[ c ] = (char)( 65 + ( ( c + 92 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id92 + a * 2 + b - (int)( m_Value92 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 123u ) ^ ( m_Flags4 << 1 ) ^ (unsigned)m_Id92; }
    bool Less( const Record92 & other ) const { return m_Id92 < other.m_Id92 || ( m_Id92 == other.m_Id92 && m_Value92 < other.m_Value92 ); }
};

template <class T> struct Box92
{
    T m_Value;
    T m_Other;
    Box92() : m_Value(), m_Other() {}
    explicit Box92( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box92 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box92<int> IntBox92;
typedef Box92<double> DoubleBox92;
inline int Total92( const IntBox92 & a, const DoubleBox92 & b, const Record92 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 92 ) + (int)r.Mix( 92u );
}
static const int g_Table92[] = { 92, 184, 277, 462, 647, 1016 };

struct Record93
{
    int m_Id93;
    double m_Value93;
    char m_Name93[ 24 ];
    Record93 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record93() : m_Id93( 93 ), m_Value93( 93.5 ), m_Next( nullptr ), m_Label( "record93" ), m_Flags5( 279u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name93 ); ++c ) { m_Name93[ c ] = (char)( 65 + ( ( c + 93 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id93 + a * 3 + b - (int)( m_Value93 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 124u ) ^ ( m_Flags5 << 2 ) ^ (unsigned)m_Id93; }
    bool Less( const Record93 & other ) const { return m_Id93 < other.m_Id93 || ( m_Id93 == other.m_Id93 && m_Value93 < other.m_Value93 ); }
};

template <class T> struct Box93
{
    T m_Value;
    T m_Other;
    Box93() : m_Value(), m_Other() {}
    explicit Box93( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box93 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box93<int> IntBox93;
typedef Box93<double> DoubleBox93;
inline int Total93( const IntBox93 & a, const DoubleBox93 & b, const Record93 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 93 ) + (int)r.Mix( 93u );
}
static const int g_Table93[] = { 93, 186, 280, 467, 654, 1027 };

struct Record94
{
    int m_Id94;
    double m_Value94;
    char m_Name94[ 28 ];
    Record94 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record94() : m_Id94( 94 ), m_Value94( 94.5 ), m_Next( nullptr ), m_Label( "record94" ), m_Flags6( 282u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name94 ); ++c ) { m_Name94[ c ] = (char)( 65 + ( ( c + 94 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id94 + a * 4 + b - (int)( m_Value94 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 125u ) ^ ( m_Flags6 << 3 ) ^ (unsigned)m_Id94; }
    bool Less( const Record94 & other ) const { return m_Id94 < other.m_Id94 || ( m_Id94 == other.m_Id94 && m_Value94 < other.m_Value94 ); }
};

template <class T> struct Box94
{
    T m_Value;
    T m_Other;
    Box94() : m_Value(), m_Other() {}
    explicit Box94( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box94 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box94<int> IntBox94;
typedef Box94<double> DoubleBox94;
inline int Total94( const IntBox94 & a, const DoubleBox94 & b, const Record94 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 94 ) + (int)r.Mix( 94u );
}
static const int g_Table94[] = { 94, 188, 283, 472, 661, 1038 };

struct Record95
{
    int m_Id95;
    double m_Value95;
    char m_Name95[ 12 ];
    Record95 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record95() : m_Id95( 95 ), m_Value95( 95.5 ), m_Next( nullptr ), m_Label( "record95" ), m_Flags7( 285u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name95 ); ++c ) { m_Name95[ c ] = (char)( 65 + ( ( c + 95 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id95 + a * 5 + b - (int)( m_Value95 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 126u ) ^ ( m_Flags7 << 4 ) ^ (unsigned)m_Id95; }
    bool Less( const Record95 & other ) const { return m_Id95 < other.m_Id95 || ( m_Id95 == other.m_Id95 && m_Value95 < other.m_Value95 ); }
};

template <class T> struct Box95
{
    T m_Value;
    T m_Other;
    Box95() : m_Value(), m_Other() {}
    explicit Box95( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box95 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box95<int> IntBox95;
typedef Box95<double> DoubleBox95;
inline int Total95( const IntBox95 & a, const DoubleBox95 & b, const Record95 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 95 ) + (int)r.Mix( 95u );
}
static const int g_Table95[] = { 95, 190, 286, 477, 668, 1049 };

struct Record96
{
    int m_Id96;
    double m_Value96;
    char m_Name96[ 16 ];
    Record96 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record96() : m_Id96( 96 ), m_Value96( 96.5 ), m_Next( nullptr ), m_Label( "record96" ), m_Flags8( 288u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name96 ); ++c ) { m_Name96[ c ] = (char)( 65 + ( ( c + 96 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id96 + a * 6 + b - (int)( m_Value96 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 127u ) ^ ( m_Flags8 << 5 ) ^ (unsigned)m_Id96; }
    bool Less( const Record96 & other ) const { return m_Id96 < other.m_Id96 || ( m_Id96 == other.m_Id96 && m_Value96 < other.m_Value96 ); }
};

template <class T> struct Box96
{
    T m_Value;
    T m_Other;
    Box96() : m_Value(), m_Other() {}
    explicit Box96( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box96 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box96<int> IntBox96;
typedef Box96<double> DoubleBox96;
inline int Total96( const IntBox96 & a, const DoubleBox96 & b, const Record96 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 96 ) + (int)r.Mix( 96u );
}
static const int g_Table96[] = { 96, 192, 289, 482, 675, 1060 };

struct Record97
{
    int m_Id97;
    double m_Value97;
    char m_Name97[ 20 ];
    Record97 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record97() : m_Id97( 97 ), m_Value97( 97.5 ), m_Next( nullptr ), m_Label( "record97" ), m_Flags9( 291u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name97 ); ++c ) { m_Name97[ c ] = (char)( 65 + ( ( c + 97 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id97 + a * 7 + b - (int)( m_Value97 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 128u ) ^ ( m_Flags9 << 6 ) ^ (unsigned)m_Id97; }
    bool Less( const Record97 & other ) const { return m_Id97 < other.m_Id97 || ( m_Id97 == other.m_Id97 && m_Value97 < other.m_Value97 ); }
};

template <class T> struct Box97
{
    T m_Value;
    T m_Other;
    Box97() : m_Value(), m_Other() {}
    explicit Box97( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box97 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box97<int> IntBox97;
typedef Box97<double> DoubleBox97;
inline int Total97( const IntBox97 & a, const DoubleBox97 & b, const Record97 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 97 ) + (int)r.Mix( 97u );
}
static const int g_Table97[] = { 97, 194, 292, 487, 682, 1071 };

struct Record98
{
    int m_Id98;
    double m_Value98;
    char m_Name98[ 24 ];
    Record98 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record98() : m_Id98( 98 ), m_Value98( 98.5 ), m_Next( nullptr ), m_Label( "record98" ), m_Flags10( 294u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name98 ); ++c ) { m_Name98[ c ] = (char)( 65 + ( ( c + 98 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id98 + a * 1 + b - (int)( m_Value98 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 129u ) ^ ( m_Flags10 << 7 ) ^ (unsigned)m_Id98; }
    bool Less( const Record98 & other ) const { return m_Id98 < other.m_Id98 || ( m_Id98 == other.m_Id98 && m_Value98 < other.m_Value98 ); }
};

template <class T> struct Box98
{
    T m_Value;
    T m_Other;
    Box98() : m_Value(), m_Other() {}
    explicit Box98( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box98 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box98<int> IntBox98;
typedef Box98<double> DoubleBox98;
inline int Total98( const IntBox98 & a, const DoubleBox98 & b, const Record98 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 98 ) + (int)r.Mix( 98u );
}
static const int g_Table98[] = { 98, 196, 295, 492, 689, 1082 };

struct Record99
{
    int m_Id99;
    double m_Value99;
    char m_Name99[ 28 ];
    Record99 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record99() : m_Id99( 99 ), m_Value99( 99.5 ), m_Next( nullptr ), m_Label( "record99" ), m_Flags0( 297u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name99 ); ++c ) { m_Name99[ c ] = (char)( 65 + ( ( c + 99 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id99 + a * 2 + b - (int)( m_Value99 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 130u ) ^ ( m_Flags0 << 8 ) ^ (unsigned)m_Id99; }
    bool Less( const Record99 & other ) const { return m_Id99 < other.m_Id99 || ( m_Id99 == other.m_Id99 && m_Value99 < other.m_Value99 ); }
};

template <class T> struct Box99
{
    T m_Value;
    T m_Other;
    Box99() : m_Value(), m_Other() {}
    explicit Box99( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box99 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box99<int> IntBox99;
typedef Box99<double> DoubleBox99;
inline int Total99( const IntBox99 & a, const DoubleBox99 & b, const Record99 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 99 ) + (int)r.Mix( 99u );
}
static const int g_Table99[] = { 99, 198, 298, 497, 696, 1093 };

struct Record100
{
    int m_Id100;
    double m_Value100;
    char m_Name100[ 12 ];
    Record100 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record100() : m_Id100( 100 ), m_Value100( 100.5 ), m_Next( nullptr ), m_Label( "record100" ), m_Flags1( 300u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name100 ); ++c ) { m_Name100[ c ] = (char)( 65 + ( ( c + 100 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id100 + a * 3 + b - (int)( m_Value100 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 131u ) ^ ( m_Flags1 << 9 ) ^ (unsigned)m_Id100; }
    bool Less( const Record100 & other ) const { return m_Id100 < other.m_Id100 || ( m_Id100 == other.m_Id100 && m_Value100 < other.m_Value100 ); }
};

template <class T> struct Box100
{
    T m_Value;
    T m_Other;
    Box100() : m_Value(), m_Other() {}
    explicit Box100( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box100 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box100<int> IntBox100;
typedef Box100<double> DoubleBox100;
inline int Total100( const IntBox100 & a, const DoubleBox100 & b, const Record100 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 100 ) + (int)r.Mix( 100u );
}
static const int g_Table100[] = { 100, 200, 301, 502, 703, 1104 };

struct Record101
{
    int m_Id101;
    double m_Value101;
    char m_Name101[ 16 ];
    Record101 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record101() : m_Id101( 101 ), m_Value101( 101.5 ), m_Next( nullptr ), m_Label( "record101" ), m_Flags2( 303u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name101 ); ++c ) { m_Name101[ c ] = (char)( 65 + ( ( c + 101 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id101 + a * 4 + b - (int)( m_Value101 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 132u ) ^ ( m_Flags2 << 10 ) ^ (unsigned)m_Id101; }
    bool Less( const Record101 & other ) const { return m_Id101 < other.m_Id101 || ( m_Id101 == other.m_Id101 && m_Value101 < other.m_Value101 ); }
};

template <class T> struct Box101
{
    T m_Value;
    T m_Other;
    Box101() : m_Value(), m_Other() {}
    explicit Box101( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box101 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box101<int> IntBox101;
typedef Box101<double> DoubleBox101;
inline int Total101( const IntBox101 & a, const DoubleBox101 & b, const Record101 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 101 ) + (int)r.Mix( 101u );
}
static const int g_Table101[] = { 101, 202, 304, 507, 710, 1115 };

struct Record102
{
    int m_Id102;
    double m_Value102;
    char m_Name102[ 20 ];
    Record102 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record102() : m_Id102( 102 ), m_Value102( 102.5 ), m_Next( nullptr ), m_Label( "record102" ), m_Flags3( 306u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name102 ); ++c ) { m_Name102[ c ] = (char)( 65 + ( ( c + 102 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id102 + a * 5 + b - (int)( m_Value102 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 133u ) ^ ( m_Flags3 << 11 ) ^ (unsigned)m_Id102; }
    bool Less( const Record102 & other ) const { return m_Id102 < other.m_Id102 || ( m_Id102 == other.m_Id102 && m_Value102 < other.m_Value102 ); }
};

template <class T> struct Box102
{
    T m_Value;
    T m_Other;
    Box102() : m_Value(), m_Other() {}
    explicit Box102( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box102 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box102<int> IntBox102;
typedef Box102<double> DoubleBox102;
inline int Total102( const IntBox102 & a, const DoubleBox102 & b, const Record102 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 102 ) + (int)r.Mix( 102u );
}
static const int g_Table102[] = { 102, 204, 307, 512, 717, 1126 };

struct Record103
{
    int m_Id103;
    double m_Value103;
    char m_Name103[ 24 ];
    Record103 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record103() : m_Id103( 103 ), m_Value103( 103.5 ), m_Next( nullptr ), m_Label( "record103" ), m_Flags4( 309u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name103 ); ++c ) { m_Name103[ c ] = (char)( 65 + ( ( c + 103 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id103 + a * 6 + b - (int)( m_Value103 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 134u ) ^ ( m_Flags4 << 12 ) ^ (unsigned)m_Id103; }
    bool Less( const Record103 & other ) const { return m_Id103 < other.m_Id103 || ( m_Id103 == other.m_Id103 && m_Value103 < other.m_Value103 ); }
};

template <class T> struct Box103
{
    T m_Value;
    T m_Other;
    Box103() : m_Value(), m_Other() {}
    explicit Box103( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box103 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box103<int> IntBox103;
typedef Box103<double> DoubleBox103;
inline int Total103( const IntBox103 & a, const DoubleBox103 & b, const Record103 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 103 ) + (int)r.Mix( 103u );
}
static const int g_Table103[] = { 103, 206, 310, 517, 724, 1137 };

struct Record104
{
    int m_Id104;
    double m_Value104;
    char m_Name104[ 28 ];
    Record104 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record104() : m_Id104( 104 ), m_Value104( 104.5 ), m_Next( nullptr ), m_Label( "record104" ), m_Flags5( 312u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name104 ); ++c ) { m_Name104[ c ] = (char)( 65 + ( ( c + 104 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id104 + a * 7 + b - (int)( m_Value104 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 135u ) ^ ( m_Flags5 << 0 ) ^ (unsigned)m_Id104; }
    bool Less( const Record104 & other ) const { return m_Id104 < other.m_Id104 || ( m_Id104 == other.m_Id104 && m_Value104 < other.m_Value104 ); }
};

template <class T> struct Box104
{
    T m_Value;
    T m_Other;
    Box104() : m_Value(), m_Other() {}
    explicit Box104( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box104 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box104<int> IntBox104;
typedef Box104<double> DoubleBox104;
inline int Total104( const IntBox104 & a, const DoubleBox104 & b, const Record104 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 104 ) + (int)r.Mix( 104u );
}
static const int g_Table104[] = { 104, 208, 313, 522, 731, 1148 };

struct Record105
{
    int m_Id105;
    double m_Value105;
    char m_Name105[ 12 ];
    Record105 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record105() : m_Id105( 105 ), m_Value105( 105.5 ), m_Next( nullptr ), m_Label( "record105" ), m_Flags6( 315u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name105 ); ++c ) { m_Name105[ c ] = (char)( 65 + ( ( c + 105 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id105 + a * 1 + b - (int)( m_Value105 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 136u ) ^ ( m_Flags6 << 1 ) ^ (unsigned)m_Id105; }
    bool Less( const Record105 & other ) const { return m_Id105 < other.m_Id105 || ( m_Id105 == other.m_Id105 && m_Value105 < other.m_Value105 ); }
};

template <class T> struct Box105
{
    T m_Value;
    T m_Other;
    Box105() : m_Value(), m_Other() {}
    explicit Box105( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box105 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box105<int> IntBox105;
typedef Box105<double> DoubleBox105;
inline int Total105( const IntBox105 & a, const DoubleBox105 & b, const Record105 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 105 ) + (int)r.Mix( 105u );
}
static const int g_Table105[] = { 105, 210, 316, 527, 738, 1159 };

struct Record106
{
    int m_Id106;
    double m_Value106;
    char m_Name106[ 16 ];
    Record106 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record106() : m_Id106( 106 ), m_Value106( 106.5 ), m_Next( nullptr ), m_Label( "record106" ), m_Flags7( 318u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name106 ); ++c ) { m_Name106[ c ] = (char)( 65 + ( ( c + 106 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id106 + a * 2 + b - (int)( m_Value106 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 137u ) ^ ( m_Flags7 << 2 ) ^ (unsigned)m_Id106; }
    bool Less( const Record106 & other ) const { return m_Id106 < other.m_Id106 || ( m_Id106 == other.m_Id106 && m_Value106 < other.m_Value106 ); }
};

template <class T> struct Box106
{
    T m_Value;
    T m_Other;
    Box106() : m_Value(), m_Other() {}
    explicit Box106( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box106 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box106<int> IntBox106;
typedef Box106<double> DoubleBox106;
inline int Total106( const IntBox106 & a, const DoubleBox106 & b, const Record106 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 106 ) + (int)r.Mix( 106u );
}
static const int g_Table106[] = { 106, 212, 319, 532, 745, 1170 };

struct Record107
{
    int m_Id107;
    double m_Value107;
    char m_Name107[ 20 ];
    Record107 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record107() : m_Id107( 107 ), m_Value107( 107.5 ), m_Next( nullptr ), m_Label( "record107" ), m_Flags8( 321u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name107 ); ++c ) { m_Name107[ c ] = (char)( 65 + ( ( c + 107 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id107 + a * 3 + b - (int)( m_Value107 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 138u ) ^ ( m_Flags8 << 3 ) ^ (unsigned)m_Id107; }
    bool Less( const Record107 & other ) const { return m_Id107 < other.m_Id107 || ( m_Id107 == other.m_Id107 && m_Value107 < other.m_Value107 ); }
};

template <class T> struct Box107
{
    T m_Value;
    T m_Other;
    Box107() : m_Value(), m_Other() {}
    explicit Box107( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box107 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box107<int> IntBox107;
typedef Box107<double> DoubleBox107;
inline int Total107( const IntBox107 & a, const DoubleBox107 & b, const Record107 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 107 ) + (int)r.Mix( 107u );
}
static const int g_Table107[] = { 107, 214, 322, 537, 752, 1181 };

struct Record108
{
    int m_Id108;
    double m_Value108;
    char m_Name108[ 24 ];
    Record108 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record108() : m_Id108( 108 ), m_Value108( 108.5 ), m_Next( nullptr ), m_Label( "record108" ), m_Flags9( 324u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name108 ); ++c ) { m_Name108[ c ] = (char)( 65 + ( ( c + 108 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id108 + a * 4 + b - (int)( m_Value108 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 139u ) ^ ( m_Flags9 << 4 ) ^ (unsigned)m_Id108; }
    bool Less( const Record108 & other ) const { return m_Id108 < other.m_Id108 || ( m_Id108 == other.m_Id108 && m_Value108 < other.m_Value108 ); }
};

template <class T> struct Box108
{
    T m_Value;
    T m_Other;
    Box108() : m_Value(), m_Other() {}
    explicit Box108( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box108 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box108<int> IntBox108;
typedef Box108<double> DoubleBox108;
inline int Total108( const IntBox108 & a, const DoubleBox108 & b, const Record108 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 108 ) + (int)r.Mix( 108u );
}
static const int g_Table108[] = { 108, 216, 325, 542, 759, 1192 };

struct Record109
{
    int m_Id109;
    double m_Value109;
    char m_Name109[ 28 ];
    Record109 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record109() : m_Id109( 109 ), m_Value109( 109.5 ), m_Next( nullptr ), m_Label( "record109" ), m_Flags10( 327u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name109 ); ++c ) { m_Name109[ c ] = (char)( 65 + ( ( c + 109 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id109 + a * 5 + b - (int)( m_Value109 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 140u ) ^ ( m_Flags10 << 5 ) ^ (unsigned)m_Id109; }
    bool Less( const Record109 & other ) const { return m_Id109 < other.m_Id109 || ( m_Id109 == other.m_Id109 && m_Value109 < other.m_Value109 ); }
};

template <class T> struct Box109
{
    T m_Value;
    T m_Other;
    Box109() : m_Value(), m_Other() {}
    explicit Box109( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box109 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box109<int> IntBox109;
typedef Box109<double> DoubleBox109;
inline int Total109( const IntBox109 & a, const DoubleBox109 & b, const Record109 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 109 ) + (int)r.Mix( 109u );
}
static const int g_Table109[] = { 109, 218, 328, 547, 766, 1203 };

struct Record110
{
    int m_Id110;
    double m_Value110;
    char m_Name110[ 12 ];
    Record110 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record110() : m_Id110( 110 ), m_Value110( 110.5 ), m_Next( nullptr ), m_Label( "record110" ), m_Flags0( 330u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name110 ); ++c ) { m_Name110[ c ] = (char)( 65 + ( ( c + 110 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id110 + a * 6 + b - (int)( m_Value110 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 141u ) ^ ( m_Flags0 << 6 ) ^ (unsigned)m_Id110; }
    bool Less( const Record110 & other ) const { return m_Id110 < other.m_Id110 || ( m_Id110 == other.m_Id110 && m_Value110 < other.m_Value110 ); }
};

template <class T> struct Box110
{
    T m_Value;
    T m_Other;
    Box110() : m_Value(), m_Other() {}
    explicit Box110( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box110 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box110<int> IntBox110;
typedef Box110<double> DoubleBox110;
inline int Total110( const IntBox110 & a, const DoubleBox110 & b, const Record110 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 110 ) + (int)r.Mix( 110u );
}
static const int g_Table110[] = { 110, 220, 331, 552, 773, 1214 };

struct Record111
{
    int m_Id111;
    double m_Value111;
    char m_Name111[ 16 ];
    Record111 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record111() : m_Id111( 111 ), m_Value111( 111.5 ), m_Next( nullptr ), m_Label( "record111" ), m_Flags1( 333u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name111 ); ++c ) { m_Name111[ c ] = (char)( 65 + ( ( c + 111 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id111 + a * 7 + b - (int)( m_Value111 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 142u ) ^ ( m_Flags1 << 7 ) ^ (unsigned)m_Id111; }
    bool Less( const Record111 & other ) const { return m_Id111 < other.m_Id111 || ( m_Id111 == other.m_Id111 && m_Value111 < other.m_Value111 ); }
};

template <class T> struct Box111
{
    T m_Value;
    T m_Other;
    Box111() : m_Value(), m_Other() {}
    explicit Box111( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box111 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box111<int> IntBox111;
typedef Box111<double> DoubleBox111;
inline int Total111( const IntBox111 & a, const DoubleBox111 & b, const Record111 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 111 ) + (int)r.Mix( 111u );
}
static const int g_Table111[] = { 111, 222, 334, 557, 780, 1225 };

struct Record112
{
    int m_Id112;
    double m_Value112;
    char m_Name112[ 20 ];
    Record112 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record112() : m_Id112( 112 ), m_Value112( 112.5 ), m_Next( nullptr ), m_Label( "record112" ), m_Flags2( 336u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name112 ); ++c ) { m_Name112[ c ] = (char)( 65 + ( ( c + 112 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id112 + a * 1 + b - (int)( m_Value112 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 143u ) ^ ( m_Flags2 << 8 ) ^ (unsigned)m_Id112; }
    bool Less( const Record112 & other ) const { return m_Id112 < other.m_Id112 || ( m_Id112 == other.m_Id112 && m_Value112 < other.m_Value112 ); }
};

template <class T> struct Box112
{
    T m_Value;
    T m_Other;
    Box112() : m_Value(), m_Other() {}
    explicit Box112( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box112 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box112<int> IntBox112;
typedef Box112<double> DoubleBox112;
inline int Total112( const IntBox112 & a, const DoubleBox112 & b, const Record112 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 112 ) + (int)r.Mix( 112u );
}
static const int g_Table112[] = { 112, 224, 337, 562, 787, 1236 };

struct Record113
{
    int m_Id113;
    double m_Value113;
    char m_Name113[ 24 ];
    Record113 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record113() : m_Id113( 113 ), m_Value113( 113.5 ), m_Next( nullptr ), m_Label( "record113" ), m_Flags3( 339u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name113 ); ++c ) { m_Name113[ c ] = (char)( 65 + ( ( c + 113 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id113 + a * 2 + b - (int)( m_Value113 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 144u ) ^ ( m_Flags3 << 9 ) ^ (unsigned)m_Id113; }
    bool Less( const Record113 & other ) const { return m_Id113 < other.m_Id113 || ( m_Id113 == other.m_Id113 && m_Value113 < other.m_Value113 ); }
};

template <class T> struct Box113
{
    T m_Value;
    T m_Other;
    Box113() : m_Value(), m_Other() {}
    explicit Box113( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box113 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box113<int> IntBox113;
typedef Box113<double> DoubleBox113;
inline int Total113( const IntBox113 & a, const DoubleBox113 & b, const Record113 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 113 ) + (int)r.Mix( 113u );
}
static const int g_Table113[] = { 113, 226, 340, 567, 794, 1247 };

struct Record114
{
    int m_Id114;
    double m_Value114;
    char m_Name114[ 28 ];
    Record114 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record114() : m_Id114( 114 ), m_Value114( 114.5 ), m_Next( nullptr ), m_Label( "record114" ), m_Flags4( 342u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name114 ); ++c ) { m_Name114[ c ] = (char)( 65 + ( ( c + 114 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id114 + a * 3 + b - (int)( m_Value114 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 145u ) ^ ( m_Flags4 << 10 ) ^ (unsigned)m_Id114; }
    bool Less( const Record114 & other ) const { return m_Id114 < other.m_Id114 || ( m_Id114 == other.m_Id114 && m_Value114 < other.m_Value114 ); }
};

template <class T> struct Box114
{
    T m_Value;
    T m_Other;
    Box114() : m_Value(), m_Other() {}
    explicit Box114( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box114 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box114<int> IntBox114;
typedef Box114<double> DoubleBox114;
inline int Total114( const IntBox114 & a, const DoubleBox114 & b, const Record114 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 114 ) + (int)r.Mix( 114u );
}
static const int g_Table114[] = { 114, 228, 343, 572, 801, 1258 };

struct Record115
{
    int m_Id115;
    double m_Value115;
    char m_Name115[ 12 ];
    Record115 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record115() : m_Id115( 115 ), m_Value115( 115.5 ), m_Next( nullptr ), m_Label( "record115" ), m_Flags5( 345u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name115 ); ++c ) { m_Name115[ c ] = (char)( 65 + ( ( c + 115 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id115 + a * 4 + b - (int)( m_Value115 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 146u ) ^ ( m_Flags5 << 11 ) ^ (unsigned)m_Id115; }
    bool Less( const Record115 & other ) const { return m_Id115 < other.m_Id115 || ( m_Id115 == other.m_Id115 && m_Value115 < other.m_Value115 ); }
};

template <class T> struct Box115
{
    T m_Value;
    T m_Other;
    Box115() : m_Value(), m_Other() {}
    explicit Box115( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box115 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box115<int> IntBox115;
typedef Box115<double> DoubleBox115;
inline int Total115( const IntBox115 & a, const DoubleBox115 & b, const Record115 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 115 ) + (int)r.Mix( 115u );
}
static const int g_Table115[] = { 115, 230, 346, 577, 808, 1269 };

struct Record116
{
    int m_Id116;
    double m_Value116;
    char m_Name116[ 16 ];
    Record116 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record116() : m_Id116( 116 ), m_Value116( 116.5 ), m_Next( nullptr ), m_Label( "record116" ), m_Flags6( 348u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name116 ); ++c ) { m_Name116[ c ] = (char)( 65 + ( ( c + 116 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id116 + a * 5 + b - (int)( m_Value116 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 147u ) ^ ( m_Flags6 << 12 ) ^ (unsigned)m_Id116; }
    bool Less( const Record116 & other ) const { return m_Id116 < other.m_Id116 || ( m_Id116 == other.m_Id116 && m_Value116 < other.m_Value116 ); }
};

template <class T> struct Box116
{
    T m_Value;
    T m_Other;
    Box116() : m_Value(), m_Other() {}
    explicit Box116( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box116 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box116<int> IntBox116;
typedef Box116<double> DoubleBox116;
inline int Total116( const IntBox116 & a, const DoubleBox116 & b, const Record116 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 116 ) + (int)r.Mix( 116u );
}
static const int g_Table116[] = { 116, 232, 349, 582, 815, 1280 };

struct Record117
{
    int m_Id117;
    double m_Value117;
    char m_Name117[ 20 ];
    Record117 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record117() : m_Id117( 117 ), m_Value117( 117.5 ), m_Next( nullptr ), m_Label( "record117" ), m_Flags7( 351u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name117 ); ++c ) { m_Name117[ c ] = (char)( 65 + ( ( c + 117 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id117 + a * 6 + b - (int)( m_Value117 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 148u ) ^ ( m_Flags7 << 0 ) ^ (unsigned)m_Id117; }
    bool Less( const Record117 & other ) const { return m_Id117 < other.m_Id117 || ( m_Id117 == other.m_Id117 && m_Value117 < other.m_Value117 ); }
};

template <class T> struct Box117
{
    T m_Value;
    T m_Other;
    Box117() : m_Value(), m_Other() {}
    explicit Box117( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box117 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box117<int> IntBox117;
typedef Box117<double> DoubleBox117;
inline int Total117( const IntBox117 & a, const DoubleBox117 & b, const Record117 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 117 ) + (int)r.Mix( 117u );
}
static const int g_Table117[] = { 117, 234, 352, 587, 822, 1291 };

struct Record118
{
    int m_Id118;
    double m_Value118;
    char m_Name118[ 24 ];
    Record118 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record118() : m_Id118( 118 ), m_Value118( 118.5 ), m_Next( nullptr ), m_Label( "record118" ), m_Flags8( 354u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name118 ); ++c ) { m_Name118[ c ] = (char)( 65 + ( ( c + 118 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id118 + a * 7 + b - (int)( m_Value118 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 149u ) ^ ( m_Flags8 << 1 ) ^ (unsigned)m_Id118; }
    bool Less( const Record118 & other ) const { return m_Id118 < other.m_Id118 || ( m_Id118 == other.m_Id118 && m_Value118 < other.m_Value118 ); }
};

template <class T> struct Box118
{
    T m_Value;
    T m_Other;
    Box118() : m_Value(), m_Other() {}
    explicit Box118( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box118 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box118<int> IntBox118;
typedef Box118<double> DoubleBox118;
inline int Total118( const IntBox118 & a, const DoubleBox118 & b, const Record118 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 118 ) + (int)r.Mix( 118u );
}
static const int g_Table118[] = { 118, 236, 355, 592, 829, 1302 };

struct Record119
{
    int m_Id119;
    double m_Value119;
    char m_Name119[ 28 ];
    Record119 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record119() : m_Id119( 119 ), m_Value119( 119.5 ), m_Next( nullptr ), m_Label( "record119" ), m_Flags9( 357u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name119 ); ++c ) { m_Name119[ c ] = (char)( 65 + ( ( c + 119 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id119 + a * 1 + b - (int)( m_Value119 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 150u ) ^ ( m_Flags9 << 2 ) ^ (unsigned)m_Id119; }
    bool Less( const Record119 & other ) const { return m_Id119 < other.m_Id119 || ( m_Id119 == other.m_Id119 && m_Value119 < other.m_Value119 ); }
};

template <class T> struct Box119
{
    T m_Value;
    T m_Other;
    Box119() : m_Value(), m_Other() {}
    explicit Box119( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box119 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box119<int> IntBox119;
typedef Box119<double> DoubleBox119;
inline int Total119( const IntBox119 & a, const DoubleBox119 & b, const Record119 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 119 ) + (int)r.Mix( 119u );
}
static const int g_Table119[] = { 119, 238, 358, 597, 836, 1313 };

struct Record120
{
    int m_Id120;
    double m_Value120;
    char m_Name120[ 12 ];
    Record120 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record120() : m_Id120( 120 ), m_Value120( 120.5 ), m_Next( nullptr ), m_Label( "record120" ), m_Flags10( 360u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name120 ); ++c ) { m_Name120[ c ] = (char)( 65 + ( ( c + 120 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id120 + a * 2 + b - (int)( m_Value120 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 151u ) ^ ( m_Flags10 << 3 ) ^ (unsigned)m_Id120; }
    bool Less( const Record120 & other ) const { return m_Id120 < other.m_Id120 || ( m_Id120 == other.m_Id120 && m_Value120 < other.m_Value120 ); }
};

template <class T> struct Box120
{
    T m_Value;
    T m_Other;
    Box120() : m_Value(), m_Other() {}
    explicit Box120( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box120 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box120<int> IntBox120;
typedef Box120<double> DoubleBox120;
inline int Total120( const IntBox120 & a, const DoubleBox120 & b, const Record120 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 120 ) + (int)r.Mix( 120u );
}
static const int g_Table120[] = { 120, 240, 361, 602, 843, 1324 };

struct Record121
{
    int m_Id121;
    double m_Value121;
    char m_Name121[ 16 ];
    Record121 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record121() : m_Id121( 121 ), m_Value121( 121.5 ), m_Next( nullptr ), m_Label( "record121" ), m_Flags0( 363u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name121 ); ++c ) { m_Name121[ c ] = (char)( 65 + ( ( c + 121 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id121 + a * 3 + b - (int)( m_Value121 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 152u ) ^ ( m_Flags0 << 4 ) ^ (unsigned)m_Id121; }
    bool Less( const Record121 & other ) const { return m_Id121 < other.m_Id121 || ( m_Id121 == other.m_Id121 && m_Value121 < other.m_Value121 ); }
};

template <class T> struct Box121
{
    T m_Value;
    T m_Other;
    Box121() : m_Value(), m_Other() {}
    explicit Box121( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box121 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box121<int> IntBox121;
typedef Box121<double> DoubleBox121;
inline int Total121( const IntBox121 & a, const DoubleBox121 & b, const Record121 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 121 ) + (int)r.Mix( 121u );
}
static const int g_Table121[] = { 121, 242, 364, 607, 850, 1335 };

struct Record122
{
    int m_Id122;
    double m_Value122;
    char m_Name122[ 20 ];
    Record122 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record122() : m_Id122( 122 ), m_Value122( 122.5 ), m_Next( nullptr ), m_Label( "record122" ), m_Flags1( 366u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name122 ); ++c ) { m_Name122[ c ] = (char)( 65 + ( ( c + 122 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id122 + a * 4 + b - (int)( m_Value122 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 153u ) ^ ( m_Flags1 << 5 ) ^ (unsigned)m_Id122; }
    bool Less( const Record122 & other ) const { return m_Id122 < other.m_Id122 || ( m_Id122 == other.m_Id122 && m_Value122 < other.m_Value122 ); }
};

template <class T> struct Box122
{
    T m_Value;
    T m_Other;
    Box122() : m_Value(), m_Other() {}
    explicit Box122( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box122 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box122<int> IntBox122;
typedef Box122<double> DoubleBox122;
inline int Total122( const IntBox122 & a, const DoubleBox122 & b, const Record122 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 122 ) + (int)r.Mix( 122u );
}
static const int g_Table122[] = { 122, 244, 367, 612, 857, 1346 };

struct Record123
{
    int m_Id123;
    double m_Value123;
    char m_Name123[ 24 ];
    Record123 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record123() : m_Id123( 123 ), m_Value123( 123.5 ), m_Next( nullptr ), m_Label( "record123" ), m_Flags2( 369u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name123 ); ++c ) { m_Name123[ c ] = (char)( 65 + ( ( c + 123 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id123 + a * 5 + b - (int)( m_Value123 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 154u ) ^ ( m_Flags2 << 6 ) ^ (unsigned)m_Id123; }
    bool Less( const Record123 & other ) const { return m_Id123 < other.m_Id123 || ( m_Id123 == other.m_Id123 && m_Value123 < other.m_Value123 ); }
};

template <class T> struct Box123
{
    T m_Value;
    T m_Other;
    Box123() : m_Value(), m_Other() {}
    explicit Box123( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box123 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box123<int> IntBox123;
typedef Box123<double> DoubleBox123;
inline int Total123( const IntBox123 & a, const DoubleBox123 & b, const Record123 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 123 ) + (int)r.Mix( 123u );
}
static const int g_Table123[] = { 123, 246, 370, 617, 864, 1357 };

struct Record124
{
    int m_Id124;
    double m_Value124;
    char m_Name124[ 28 ];
    Record124 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record124() : m_Id124( 124 ), m_Value124( 124.5 ), m_Next( nullptr ), m_Label( "record124" ), m_Flags3( 372u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name124 ); ++c ) { m_Name124[ c ] = (char)( 65 + ( ( c + 124 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id124 + a * 6 + b - (int)( m_Value124 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 155u ) ^ ( m_Flags3 << 7 ) ^ (unsigned)m_Id124; }
    bool Less( const Record124 & other ) const { return m_Id124 < other.m_Id124 || ( m_Id124 == other.m_Id124 && m_Value124 < other.m_Value124 ); }
};

template <class T> struct Box124
{
    T m_Value;
    T m_Other;
    Box124() : m_Value(), m_Other() {}
    explicit Box124( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box124 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box124<int> IntBox124;
typedef Box124<double> DoubleBox124;
inline int Total124( const IntBox124 & a, const DoubleBox124 & b, const Record124 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 124 ) + (int)r.Mix( 124u );
}
static const int g_Table124[] = { 124, 248, 373, 622, 871, 1368 };

struct Record125
{
    int m_Id125;
    double m_Value125;
    char m_Name125[ 12 ];
    Record125 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record125() : m_Id125( 125 ), m_Value125( 125.5 ), m_Next( nullptr ), m_Label( "record125" ), m_Flags4( 375u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name125 ); ++c ) { m_Name125[ c ] = (char)( 65 + ( ( c + 125 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id125 + a * 7 + b - (int)( m_Value125 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 156u ) ^ ( m_Flags4 << 8 ) ^ (unsigned)m_Id125; }
    bool Less( const Record125 & other ) const { return m_Id125 < other.m_Id125 || ( m_Id125 == other.m_Id125 && m_Value125 < other.m_Value125 ); }
};

template <class T> struct Box125
{
    T m_Value;
    T m_Other;
    Box125() : m_Value(), m_Other() {}
    explicit Box125( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box125 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box125<int> IntBox125;
typedef Box125<double> DoubleBox125;
inline int Total125( const IntBox125 & a, const DoubleBox125 & b, const Record125 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 125 ) + (int)r.Mix( 125u );
}
static const int g_Table125[] = { 125, 250, 376, 627, 878, 1379 };

struct Record126
{
    int m_Id126;
    double m_Value126;
    char m_Name126[ 16 ];
    Record126 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record126() : m_Id126( 126 ), m_Value126( 126.5 ), m_Next( nullptr ), m_Label( "record126" ), m_Flags5( 378u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name126 ); ++c ) { m_Name126[ c ] = (char)( 65 + ( ( c + 126 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id126 + a * 1 + b - (int)( m_Value126 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 157u ) ^ ( m_Flags5 << 9 ) ^ (unsigned)m_Id126; }
    bool Less( const Record126 & other ) const { return m_Id126 < other.m_Id126 || ( m_Id126 == other.m_Id126 && m_Value126 < other.m_Value126 ); }
};

template <class T> struct Box126
{
    T m_Value;
    T m_Other;
    Box126() : m_Value(), m_Other() {}
    explicit Box126( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box126 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box126<int> IntBox126;
typedef Box126<double> DoubleBox126;
inline int Total126( const IntBox126 & a, const DoubleBox126 & b, const Record126 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 126 ) + (int)r.Mix( 126u );
}
static const int g_Table126[] = { 126, 252, 379, 632, 885, 1390 };

struct Record127
{
    int m_Id127;
    double m_Value127;
    char m_Name127[ 20 ];
    Record127 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record127() : m_Id127( 127 ), m_Value127( 127.5 ), m_Next( nullptr ), m_Label( "record127" ), m_Flags6( 381u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name127 ); ++c ) { m_Name127[ c ] = (char)( 65 + ( ( c + 127 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id127 + a * 2 + b - (int)( m_Value127 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 158u ) ^ ( m_Flags6 << 10 ) ^ (unsigned)m_Id127; }
    bool Less( const Record127 & other ) const { return m_Id127 < other.m_Id127 || ( m_Id127 == other.m_Id127 && m_Value127 < other.m_Value127 ); }
};

template <class T> struct Box127
{
    T m_Value;
    T m_Other;
    Box127() : m_Value(), m_Other() {}
    explicit Box127( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box127 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box127<int> IntBox127;
typedef Box127<double> DoubleBox127;
inline int Total127( const IntBox127 & a, const DoubleBox127 & b, const Record127 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 127 ) + (int)r.Mix( 127u );
}
static const int g_Table127[] = { 127, 254, 382, 637, 892, 1401 };

struct Record128
{
    int m_Id128;
    double m_Value128;
    char m_Name128[ 24 ];
    Record128 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record128() : m_Id128( 128 ), m_Value128( 128.5 ), m_Next( nullptr ), m_Label( "record128" ), m_Flags7( 384u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name128 ); ++c ) { m_Name128[ c ] = (char)( 65 + ( ( c + 128 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id128 + a * 3 + b - (int)( m_Value128 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 159u ) ^ ( m_Flags7 << 11 ) ^ (unsigned)m_Id128; }
    bool Less( const Record128 & other ) const { return m_Id128 < other.m_Id128 || ( m_Id128 == other.m_Id128 && m_Value128 < other.m_Value128 ); }
};

template <class T> struct Box128
{
    T m_Value;
    T m_Other;
    Box128() : m_Value(), m_Other() {}
    explicit Box128( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box128 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box128<int> IntBox128;
typedef Box128<double> DoubleBox128;
inline int Total128( const IntBox128 & a, const DoubleBox128 & b, const Record128 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 128 ) + (int)r.Mix( 128u );
}
static const int g_Table128[] = { 128, 256, 385, 642, 899, 1412 };

struct Record129
{
    int m_Id129;
    double m_Value129;
    char m_Name129[ 28 ];
    Record129 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record129() : m_Id129( 129 ), m_Value129( 129.5 ), m_Next( nullptr ), m_Label( "record129" ), m_Flags8( 387u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name129 ); ++c ) { m_Name129[ c ] = (char)( 65 + ( ( c + 129 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id129 + a * 4 + b - (int)( m_Value129 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 160u ) ^ ( m_Flags8 << 12 ) ^ (unsigned)m_Id129; }
    bool Less( const Record129 & other ) const { return m_Id129 < other.m_Id129 || ( m_Id129 == other.m_Id129 && m_Value129 < other.m_Value129 ); }
};

template <class T> struct Box129
{
    T m_Value;
    T m_Other;
    Box129() : m_Value(), m_Other() {}
    explicit Box129( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box129 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box129<int> IntBox129;
typedef Box129<double> DoubleBox129;
inline int Total129( const IntBox129 & a, const DoubleBox129 & b, const Record129 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 129 ) + (int)r.Mix( 129u );
}
static const int g_Table129[] = { 129, 258, 388, 647, 906, 1423 };

struct Record130
{
    int m_Id130;
    double m_Value130;
    char m_Name130[ 12 ];
    Record130 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record130() : m_Id130( 130 ), m_Value130( 130.5 ), m_Next( nullptr ), m_Label( "record130" ), m_Flags9( 390u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name130 ); ++c ) { m_Name130[ c ] = (char)( 65 + ( ( c + 130 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id130 + a * 5 + b - (int)( m_Value130 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 161u ) ^ ( m_Flags9 << 0 ) ^ (unsigned)m_Id130; }
    bool Less( const Record130 & other ) const { return m_Id130 < other.m_Id130 || ( m_Id130 == other.m_Id130 && m_Value130 < other.m_Value130 ); }
};

template <class T> struct Box130
{
    T m_Value;
    T m_Other;
    Box130() : m_Value(), m_Other() {}
    explicit Box130( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box130 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box130<int> IntBox130;
typedef Box130<double> DoubleBox130;
inline int Total130( const IntBox130 & a, const DoubleBox130 & b, const Record130 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 130 ) + (int)r.Mix( 130u );
}
static const int g_Table130[] = { 130, 260, 391, 652, 913, 1434 };

struct Record131
{
    int m_Id131;
    double m_Value131;
    char m_Name131[ 16 ];
    Record131 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record131() : m_Id131( 131 ), m_Value131( 131.5 ), m_Next( nullptr ), m_Label( "record131" ), m_Flags10( 393u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name131 ); ++c ) { m_Name131[ c ] = (char)( 65 + ( ( c + 131 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id131 + a * 6 + b - (int)( m_Value131 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 162u ) ^ ( m_Flags10 << 1 ) ^ (unsigned)m_Id131; }
    bool Less( const Record131 & other ) const { return m_Id131 < other.m_Id131 || ( m_Id131 == other.m_Id131 && m_Value131 < other.m_Value131 ); }
};

template <class T> struct Box131
{
    T m_Value;
    T m_Other;
    Box131() : m_Value(), m_Other() {}
    explicit Box131( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box131 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box131<int> IntBox131;
typedef Box131<double> DoubleBox131;
inline int Total131( const IntBox131 & a, const DoubleBox131 & b, const Record131 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 131 ) + (int)r.Mix( 131u );
}
static const int g_Table131[] = { 131, 262, 394, 657, 920, 1445 };

struct Record132
{
    int m_Id132;
    double m_Value132;
    char m_Name132[ 20 ];
    Record132 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record132() : m_Id132( 132 ), m_Value132( 132.5 ), m_Next( nullptr ), m_Label( "record132" ), m_Flags0( 396u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name132 ); ++c ) { m_Name132[ c ] = (char)( 65 + ( ( c + 132 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id132 + a * 7 + b - (int)( m_Value132 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 163u ) ^ ( m_Flags0 << 2 ) ^ (unsigned)m_Id132; }
    bool Less( const Record132 & other ) const { return m_Id132 < other.m_Id132 || ( m_Id132 == other.m_Id132 && m_Value132 < other.m_Value132 ); }
};

template <class T> struct Box132
{
    T m_Value;
    T m_Other;
    Box132() : m_Value(), m_Other() {}
    explicit Box132( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box132 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box132<int> IntBox132;
typedef Box132<double> DoubleBox132;
inline int Total132( const IntBox132 & a, const DoubleBox132 & b, const Record132 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 132 ) + (int)r.Mix( 132u );
}
static const int g_Table132[] = { 132, 264, 397, 662, 927, 1456 };

struct Record133
{
    int m_Id133;
    double m_Value133;
    char m_Name133[ 24 ];
    Record133 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record133() : m_Id133( 133 ), m_Value133( 133.5 ), m_Next( nullptr ), m_Label( "record133" ), m_Flags1( 399u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name133 ); ++c ) { m_Name133[ c ] = (char)( 65 + ( ( c + 133 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id133 + a * 1 + b - (int)( m_Value133 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 164u ) ^ ( m_Flags1 << 3 ) ^ (unsigned)m_Id133; }
    bool Less( const Record133 & other ) const { return m_Id133 < other.m_Id133 || ( m_Id133 == other.m_Id133 && m_Value133 < other.m_Value133 ); }
};

template <class T> struct Box133
{
    T m_Value;
    T m_Other;
    Box133() : m_Value(), m_Other() {}
    explicit Box133( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box133 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box133<int> IntBox133;
typedef Box133<double> DoubleBox133;
inline int Total133( const IntBox133 & a, const DoubleBox133 & b, const Record133 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 133 ) + (int)r.Mix( 133u );
}
static const int g_Table133[] = { 133, 266, 400, 667, 934, 1467 };

struct Record134
{
    int m_Id134;
    double m_Value134;
    char m_Name134[ 28 ];
    Record134 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record134() : m_Id134( 134 ), m_Value134( 134.5 ), m_Next( nullptr ), m_Label( "record134" ), m_Flags2( 402u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name134 ); ++c ) { m_Name134[ c ] = (char)( 65 + ( ( c + 134 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id134 + a * 2 + b - (int)( m_Value134 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 165u ) ^ ( m_Flags2 << 4 ) ^ (unsigned)m_Id134; }
    bool Less( const Record134 & other ) const { return m_Id134 < other.m_Id134 || ( m_Id134 == other.m_Id134 && m_Value134 < other.m_Value134 ); }
};

template <class T> struct Box134
{
    T m_Value;
    T m_Other;
    Box134() : m_Value(), m_Other() {}
    explicit Box134( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box134 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box134<int> IntBox134;
typedef Box134<double> DoubleBox134;
inline int Total134( const IntBox134 & a, const DoubleBox134 & b, const Record134 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 134 ) + (int)r.Mix( 134u );
}
static const int g_Table134[] = { 134, 268, 403, 672, 941, 1478 };

struct Record135
{
    int m_Id135;
    double m_Value135;
    char m_Name135[ 12 ];
    Record135 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record135() : m_Id135( 135 ), m_Value135( 135.5 ), m_Next( nullptr ), m_Label( "record135" ), m_Flags3( 405u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name135 ); ++c ) { m_Name135[ c ] = (char)( 65 + ( ( c + 135 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id135 + a * 3 + b - (int)( m_Value135 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 166u ) ^ ( m_Flags3 << 5 ) ^ (unsigned)m_Id135; }
    bool Less( const Record135 & other ) const { return m_Id135 < other.m_Id135 || ( m_Id135 == other.m_Id135 && m_Value135 < other.m_Value135 ); }
};

template <class T> struct Box135
{
    T m_Value;
    T m_Other;
    Box135() : m_Value(), m_Other() {}
    explicit Box135( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box135 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box135<int> IntBox135;
typedef Box135<double> DoubleBox135;
inline int Total135( const IntBox135 & a, const DoubleBox135 & b, const Record135 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 135 ) + (int)r.Mix( 135u );
}
static const int g_Table135[] = { 135, 270, 406, 677, 948, 1489 };

struct Record136
{
    int m_Id136;
    double m_Value136;
    char m_Name136[ 16 ];
    Record136 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record136() : m_Id136( 136 ), m_Value136( 136.5 ), m_Next( nullptr ), m_Label( "record136" ), m_Flags4( 408u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name136 ); ++c ) { m_Name136[ c ] = (char)( 65 + ( ( c + 136 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id136 + a * 4 + b - (int)( m_Value136 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 167u ) ^ ( m_Flags4 << 6 ) ^ (unsigned)m_Id136; }
    bool Less( const Record136 & other ) const { return m_Id136 < other.m_Id136 || ( m_Id136 == other.m_Id136 && m_Value136 < other.m_Value136 ); }
};

template <class T> struct Box136
{
    T m_Value;
    T m_Other;
    Box136() : m_Value(), m_Other() {}
    explicit Box136( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box136 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box136<int> IntBox136;
typedef Box136<double> DoubleBox136;
inline int Total136( const IntBox136 & a, const DoubleBox136 & b, const Record136 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 136 ) + (int)r.Mix( 136u );
}
static const int g_Table136[] = { 136, 272, 409, 682, 955, 1500 };

struct Record137
{
    int m_Id137;
    double m_Value137;
    char m_Name137[ 20 ];
    Record137 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record137() : m_Id137( 137 ), m_Value137( 137.5 ), m_Next( nullptr ), m_Label( "record137" ), m_Flags5( 411u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name137 ); ++c ) { m_Name137[ c ] = (char)( 65 + ( ( c + 137 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id137 + a * 5 + b - (int)( m_Value137 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 168u ) ^ ( m_Flags5 << 7 ) ^ (unsigned)m_Id137; }
    bool Less( const Record137 & other ) const { return m_Id137 < other.m_Id137 || ( m_Id137 == other.m_Id137 && m_Value137 < other.m_Value137 ); }
};

template <class T> struct Box137
{
    T m_Value;
    T m_Other;
    Box137() : m_Value(), m_Other() {}
    explicit Box137( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box137 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box137<int> IntBox137;
typedef Box137<double> DoubleBox137;
inline int Total137( const IntBox137 & a, const DoubleBox137 & b, const Record137 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 137 ) + (int)r.Mix( 137u );
}
static const int g_Table137[] = { 137, 274, 412, 687, 962, 1511 };

struct Record138
{
    int m_Id138;
    double m_Value138;
    char m_Name138[ 24 ];
    Record138 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record138() : m_Id138( 138 ), m_Value138( 138.5 ), m_Next( nullptr ), m_Label( "record138" ), m_Flags6( 414u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name138 ); ++c ) { m_Name138[ c ] = (char)( 65 + ( ( c + 138 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id138 + a * 6 + b - (int)( m_Value138 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 169u ) ^ ( m_Flags6 << 8 ) ^ (unsigned)m_Id138; }
    bool Less( const Record138 & other ) const { return m_Id138 < other.m_Id138 || ( m_Id138 == other.m_Id138 && m_Value138 < other.m_Value138 ); }
};

template <class T> struct Box138
{
    T m_Value;
    T m_Other;
    Box138() : m_Value(), m_Other() {}
    explicit Box138( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box138 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box138<int> IntBox138;
typedef Box138<double> DoubleBox138;
inline int Total138( const IntBox138 & a, const DoubleBox138 & b, const Record138 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 138 ) + (int)r.Mix( 138u );
}
static const int g_Table138[] = { 138, 276, 415, 692, 969, 1522 };

struct Record139
{
    int m_Id139;
    double m_Value139;
    char m_Name139[ 28 ];
    Record139 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record139() : m_Id139( 139 ), m_Value139( 139.5 ), m_Next( nullptr ), m_Label( "record139" ), m_Flags7( 417u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name139 ); ++c ) { m_Name139[ c ] = (char)( 65 + ( ( c + 139 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id139 + a * 7 + b - (int)( m_Value139 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 170u ) ^ ( m_Flags7 << 9 ) ^ (unsigned)m_Id139; }
    bool Less( const Record139 & other ) const { return m_Id139 < other.m_Id139 || ( m_Id139 == other.m_Id139 && m_Value139 < other.m_Value139 ); }
};

template <class T> struct Box139
{
    T m_Value;
    T m_Other;
    Box139() : m_Value(), m_Other() {}
    explicit Box139( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box139 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box139<int> IntBox139;
typedef Box139<double> DoubleBox139;
inline int Total139( const IntBox139 & a, const DoubleBox139 & b, const Record139 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 139 ) + (int)r.Mix( 139u );
}
static const int g_Table139[] = { 139, 278, 418, 697, 976, 1533 };

struct Record140
{
    int m_Id140;
    double m_Value140;
    char m_Name140[ 12 ];
    Record140 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record140() : m_Id140( 140 ), m_Value140( 140.5 ), m_Next( nullptr ), m_Label( "record140" ), m_Flags8( 420u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name140 ); ++c ) { m_Name140[ c ] = (char)( 65 + ( ( c + 140 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id140 + a * 1 + b - (int)( m_Value140 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 171u ) ^ ( m_Flags8 << 10 ) ^ (unsigned)m_Id140; }
    bool Less( const Record140 & other ) const { return m_Id140 < other.m_Id140 || ( m_Id140 == other.m_Id140 && m_Value140 < other.m_Value140 ); }
};

template <class T> struct Box140
{
    T m_Value;
    T m_Other;
    Box140() : m_Value(), m_Other() {}
    explicit Box140( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box140 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box140<int> IntBox140;
typedef Box140<double> DoubleBox140;
inline int Total140( const IntBox140 & a, const DoubleBox140 & b, const Record140 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 140 ) + (int)r.Mix( 140u );
}
static const int g_Table140[] = { 140, 280, 421, 702, 983, 1544 };

struct Record141
{
    int m_Id141;
    double m_Value141;
    char m_Name141[ 16 ];
    Record141 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record141() : m_Id141( 141 ), m_Value141( 141.5 ), m_Next( nullptr ), m_Label( "record141" ), m_Flags9( 423u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name141 ); ++c ) { m_Name141[ c ] = (char)( 65 + ( ( c + 141 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id141 + a * 2 + b - (int)( m_Value141 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 172u ) ^ ( m_Flags9 << 11 ) ^ (unsigned)m_Id141; }
    bool Less( const Record141 & other ) const { return m_Id141 < other.m_Id141 || ( m_Id141 == other.m_Id141 && m_Value141 < other.m_Value141 ); }
};

template <class T> struct Box141
{
    T m_Value;
    T m_Other;
    Box141() : m_Value(), m_Other() {}
    explicit Box141( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box141 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box141<int> IntBox141;
typedef Box141<double> DoubleBox141;
inline int Total141( const IntBox141 & a, const DoubleBox141 & b, const Record141 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 141 ) + (int)r.Mix( 141u );
}
static const int g_Table141[] = { 141, 282, 424, 707, 990, 1555 };

struct Record142
{
    int m_Id142;
    double m_Value142;
    char m_Name142[ 20 ];
    Record142 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record142() : m_Id142( 142 ), m_Value142( 142.5 ), m_Next( nullptr ), m_Label( "record142" ), m_Flags10( 426u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name142 ); ++c ) { m_Name142[ c ] = (char)( 65 + ( ( c + 142 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id142 + a * 3 + b - (int)( m_Value142 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 173u ) ^ ( m_Flags10 << 12 ) ^ (unsigned)m_Id142; }
    bool Less( const Record142 & other ) const { return m_Id142 < other.m_Id142 || ( m_Id142 == other.m_Id142 && m_Value142 < other.m_Value142 ); }
};

template <class T> struct Box142
{
    T m_Value;
    T m_Other;
    Box142() : m_Value(), m_Other() {}
    explicit Box142( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box142 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box142<int> IntBox142;
typedef Box142<double> DoubleBox142;
inline int Total142( const IntBox142 & a, const DoubleBox142 & b, const Record142 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 142 ) + (int)r.Mix( 142u );
}
static const int g_Table142[] = { 142, 284, 427, 712, 997, 1566 };

struct Record143
{
    int m_Id143;
    double m_Value143;
    char m_Name143[ 24 ];
    Record143 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record143() : m_Id143( 143 ), m_Value143( 143.5 ), m_Next( nullptr ), m_Label( "record143" ), m_Flags0( 429u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name143 ); ++c ) { m_Name143[ c ] = (char)( 65 + ( ( c + 143 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id143 + a * 4 + b - (int)( m_Value143 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 174u ) ^ ( m_Flags0 << 0 ) ^ (unsigned)m_Id143; }
    bool Less( const Record143 & other ) const { return m_Id143 < other.m_Id143 || ( m_Id143 == other.m_Id143 && m_Value143 < other.m_Value143 ); }
};

template <class T> struct Box143
{
    T m_Value;
    T m_Other;
    Box143() : m_Value(), m_Other() {}
    explicit Box143( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box143 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box143<int> IntBox143;
typedef Box143<double> DoubleBox143;
inline int Total143( const IntBox143 & a, const DoubleBox143 & b, const Record143 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 143 ) + (int)r.Mix( 143u );
}
static const int g_Table143[] = { 143, 286, 430, 717, 1004, 1577 };

struct Record144
{
    int m_Id144;
    double m_Value144;
    char m_Name144[ 28 ];
    Record144 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record144() : m_Id144( 144 ), m_Value144( 144.5 ), m_Next( nullptr ), m_Label( "record144" ), m_Flags1( 432u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name144 ); ++c ) { m_Name144[ c ] = (char)( 65 + ( ( c + 144 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id144 + a * 5 + b - (int)( m_Value144 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 175u ) ^ ( m_Flags1 << 1 ) ^ (unsigned)m_Id144; }
    bool Less( const Record144 & other ) const { return m_Id144 < other.m_Id144 || ( m_Id144 == other.m_Id144 && m_Value144 < other.m_Value144 ); }
};

template <class T> struct Box144
{
    T m_Value;
    T m_Other;
    Box144() : m_Value(), m_Other() {}
    explicit Box144( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box144 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box144<int> IntBox144;
typedef Box144<double> DoubleBox144;
inline int Total144( const IntBox144 & a, const DoubleBox144 & b, const Record144 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 144 ) + (int)r.Mix( 144u );
}
static const int g_Table144[] = { 144, 288, 433, 722, 1011, 1588 };

struct Record145
{
    int m_Id145;
    double m_Value145;
    char m_Name145[ 12 ];
    Record145 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record145() : m_Id145( 145 ), m_Value145( 145.5 ), m_Next( nullptr ), m_Label( "record145" ), m_Flags2( 435u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name145 ); ++c ) { m_Name145[ c ] = (char)( 65 + ( ( c + 145 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id145 + a * 6 + b - (int)( m_Value145 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 176u ) ^ ( m_Flags2 << 2 ) ^ (unsigned)m_Id145; }
    bool Less( const Record145 & other ) const { return m_Id145 < other.m_Id145 || ( m_Id145 == other.m_Id145 && m_Value145 < other.m_Value145 ); }
};

template <class T> struct Box145
{
    T m_Value;
    T m_Other;
    Box145() : m_Value(), m_Other() {}
    explicit Box145( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box145 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box145<int> IntBox145;
typedef Box145<double> DoubleBox145;
inline int Total145( const IntBox145 & a, const DoubleBox145 & b, const Record145 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 145 ) + (int)r.Mix( 145u );
}
static const int g_Table145[] = { 145, 290, 436, 727, 1018, 1599 };

struct Record146
{
    int m_Id146;
    double m_Value146;
    char m_Name146[ 16 ];
    Record146 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record146() : m_Id146( 146 ), m_Value146( 146.5 ), m_Next( nullptr ), m_Label( "record146" ), m_Flags3( 438u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name146 ); ++c ) { m_Name146[ c ] = (char)( 65 + ( ( c + 146 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id146 + a * 7 + b - (int)( m_Value146 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 177u ) ^ ( m_Flags3 << 3 ) ^ (unsigned)m_Id146; }
    bool Less( const Record146 & other ) const { return m_Id146 < other.m_Id146 || ( m_Id146 == other.m_Id146 && m_Value146 < other.m_Value146 ); }
};

template <class T> struct Box146
{
    T m_Value;
    T m_Other;
    Box146() : m_Value(), m_Other() {}
    explicit Box146( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box146 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box146<int> IntBox146;
typedef Box146<double> DoubleBox146;
inline int Total146( const IntBox146 & a, const DoubleBox146 & b, const Record146 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 146 ) + (int)r.Mix( 146u );
}
static const int g_Table146[] = { 146, 292, 439, 732, 1025, 1610 };

struct Record147
{
    int m_Id147;
    double m_Value147;
    char m_Name147[ 20 ];
    Record147 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record147() : m_Id147( 147 ), m_Value147( 147.5 ), m_Next( nullptr ), m_Label( "record147" ), m_Flags4( 441u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name147 ); ++c ) { m_Name147[ c ] = (char)( 65 + ( ( c + 147 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id147 + a * 1 + b - (int)( m_Value147 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 178u ) ^ ( m_Flags4 << 4 ) ^ (unsigned)m_Id147; }
    bool Less( const Record147 & other ) const { return m_Id147 < other.m_Id147 || ( m_Id147 == other.m_Id147 && m_Value147 < other.m_Value147 ); }
};

template <class T> struct Box147
{
    T m_Value;
    T m_Other;
    Box147() : m_Value(), m_Other() {}
    explicit Box147( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box147 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box147<int> IntBox147;
typedef Box147<double> DoubleBox147;
inline int Total147( const IntBox147 & a, const DoubleBox147 & b, const Record147 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 147 ) + (int)r.Mix( 147u );
}
static const int g_Table147[] = { 147, 294, 442, 737, 1032, 1621 };

struct Record148
{
    int m_Id148;
    double m_Value148;
    char m_Name148[ 24 ];
    Record148 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record148() : m_Id148( 148 ), m_Value148( 148.5 ), m_Next( nullptr ), m_Label( "record148" ), m_Flags5( 444u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name148 ); ++c ) { m_Name148[ c ] = (char)( 65 + ( ( c + 148 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id148 + a * 2 + b - (int)( m_Value148 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 179u ) ^ ( m_Flags5 << 5 ) ^ (unsigned)m_Id148; }
    bool Less( const Record148 & other ) const { return m_Id148 < other.m_Id148 || ( m_Id148 == other.m_Id148 && m_Value148 < other.m_Value148 ); }
};

template <class T> struct Box148
{
    T m_Value;
    T m_Other;
    Box148() : m_Value(), m_Other() {}
    explicit Box148( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box148 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box148<int> IntBox148;
typedef Box148<double> DoubleBox148;
inline int Total148( const IntBox148 & a, const DoubleBox148 & b, const Record148 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 148 ) + (int)r.Mix( 148u );
}
static const int g_Table148[] = { 148, 296, 445, 742, 1039, 1632 };

struct Record149
{
    int m_Id149;
    double m_Value149;
    char m_Name149[ 28 ];
    Record149 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record149() : m_Id149( 149 ), m_Value149( 149.5 ), m_Next( nullptr ), m_Label( "record149" ), m_Flags6( 447u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name149 ); ++c ) { m_Name149[ c ] = (char)( 65 + ( ( c + 149 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id149 + a * 3 + b - (int)( m_Value149 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 180u ) ^ ( m_Flags6 << 6 ) ^ (unsigned)m_Id149; }
    bool Less( const Record149 & other ) const { return m_Id149 < other.m_Id149 || ( m_Id149 == other.m_Id149 && m_Value149 < other.m_Value149 ); }
};

template <class T> struct Box149
{
    T m_Value;
    T m_Other;
    Box149() : m_Value(), m_Other() {}
    explicit Box149( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box149 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box149<int> IntBox149;
typedef Box149<double> DoubleBox149;
inline int Total149( const IntBox149 & a, const DoubleBox149 & b, const Record149 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 149 ) + (int)r.Mix( 149u );
}
static const int g_Table149[] = { 149, 298, 448, 747, 1046, 1643 };

struct Record150
{
    int m_Id150;
    double m_Value150;
    char m_Name150[ 12 ];
    Record150 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record150() : m_Id150( 150 ), m_Value150( 150.5 ), m_Next( nullptr ), m_Label( "record150" ), m_Flags7( 450u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name150 ); ++c ) { m_Name150[ c ] = (char)( 65 + ( ( c + 150 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id150 + a * 4 + b - (int)( m_Value150 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 181u ) ^ ( m_Flags7 << 7 ) ^ (unsigned)m_Id150; }
    bool Less( const Record150 & other ) const { return m_Id150 < other.m_Id150 || ( m_Id150 == other.m_Id150 && m_Value150 < other.m_Value150 ); }
};

template <class T> struct Box150
{
    T m_Value;
    T m_Other;
    Box150() : m_Value(), m_Other() {}
    explicit Box150( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box150 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box150<int> IntBox150;
typedef Box150<double> DoubleBox150;
inline int Total150( const IntBox150 & a, const DoubleBox150 & b, const Record150 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 150 ) + (int)r.Mix( 150u );
}
static const int g_Table150[] = { 150, 300, 451, 752, 1053, 1654 };

struct Record151
{
    int m_Id151;
    double m_Value151;
    char m_Name151[ 16 ];
    Record151 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record151() : m_Id151( 151 ), m_Value151( 151.5 ), m_Next( nullptr ), m_Label( "record151" ), m_Flags8( 453u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name151 ); ++c ) { m_Name151[ c ] = (char)( 65 + ( ( c + 151 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id151 + a * 5 + b - (int)( m_Value151 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 182u ) ^ ( m_Flags8 << 8 ) ^ (unsigned)m_Id151; }
    bool Less( const Record151 & other ) const { return m_Id151 < other.m_Id151 || ( m_Id151 == other.m_Id151 && m_Value151 < other.m_Value151 ); }
};

template <class T> struct Box151
{
    T m_Value;
    T m_Other;
    Box151() : m_Value(), m_Other() {}
    explicit Box151( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box151 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box151<int> IntBox151;
typedef Box151<double> DoubleBox151;
inline int Total151( const IntBox151 & a, const DoubleBox151 & b, const Record151 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 151 ) + (int)r.Mix( 151u );
}
static const int g_Table151[] = { 151, 302, 454, 757, 1060, 1665 };

struct Record152
{
    int m_Id152;
    double m_Value152;
    char m_Name152[ 20 ];
    Record152 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record152() : m_Id152( 152 ), m_Value152( 152.5 ), m_Next( nullptr ), m_Label( "record152" ), m_Flags9( 456u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name152 ); ++c ) { m_Name152[ c ] = (char)( 65 + ( ( c + 152 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id152 + a * 6 + b - (int)( m_Value152 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 183u ) ^ ( m_Flags9 << 9 ) ^ (unsigned)m_Id152; }
    bool Less( const Record152 & other ) const { return m_Id152 < other.m_Id152 || ( m_Id152 == other.m_Id152 && m_Value152 < other.m_Value152 ); }
};

template <class T> struct Box152
{
    T m_Value;
    T m_Other;
    Box152() : m_Value(), m_Other() {}
    explicit Box152( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box152 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box152<int> IntBox152;
typedef Box152<double> DoubleBox152;
inline int Total152( const IntBox152 & a, const DoubleBox152 & b, const Record152 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 152 ) + (int)r.Mix( 152u );
}
static const int g_Table152[] = { 152, 304, 457, 762, 1067, 1676 };

struct Record153
{
    int m_Id153;
    double m_Value153;
    char m_Name153[ 24 ];
    Record153 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record153() : m_Id153( 153 ), m_Value153( 153.5 ), m_Next( nullptr ), m_Label( "record153" ), m_Flags10( 459u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name153 ); ++c ) { m_Name153[ c ] = (char)( 65 + ( ( c + 153 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id153 + a * 7 + b - (int)( m_Value153 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 184u ) ^ ( m_Flags10 << 10 ) ^ (unsigned)m_Id153; }
    bool Less( const Record153 & other ) const { return m_Id153 < other.m_Id153 || ( m_Id153 == other.m_Id153 && m_Value153 < other.m_Value153 ); }
};

template <class T> struct Box153
{
    T m_Value;
    T m_Other;
    Box153() : m_Value(), m_Other() {}
    explicit Box153( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box153 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box153<int> IntBox153;
typedef Box153<double> DoubleBox153;
inline int Total153( const IntBox153 & a, const DoubleBox153 & b, const Record153 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 153 ) + (int)r.Mix( 153u );
}
static const int g_Table153[] = { 153, 306, 460, 767, 1074, 1687 };

struct Record154
{
    int m_Id154;
    double m_Value154;
    char m_Name154[ 28 ];
    Record154 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record154() : m_Id154( 154 ), m_Value154( 154.5 ), m_Next( nullptr ), m_Label( "record154" ), m_Flags0( 462u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name154 ); ++c ) { m_Name154[ c ] = (char)( 65 + ( ( c + 154 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id154 + a * 1 + b - (int)( m_Value154 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 185u ) ^ ( m_Flags0 << 11 ) ^ (unsigned)m_Id154; }
    bool Less( const Record154 & other ) const { return m_Id154 < other.m_Id154 || ( m_Id154 == other.m_Id154 && m_Value154 < other.m_Value154 ); }
};

template <class T> struct Box154
{
    T m_Value;
    T m_Other;
    Box154() : m_Value(), m_Other() {}
    explicit Box154( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box154 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box154<int> IntBox154;
typedef Box154<double> DoubleBox154;
inline int Total154( const IntBox154 & a, const DoubleBox154 & b, const Record154 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 154 ) + (int)r.Mix( 154u );
}
static const int g_Table154[] = { 154, 308, 463, 772, 1081, 1698 };

struct Record155
{
    int m_Id155;
    double m_Value155;
    char m_Name155[ 12 ];
    Record155 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record155() : m_Id155( 155 ), m_Value155( 155.5 ), m_Next( nullptr ), m_Label( "record155" ), m_Flags1( 465u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name155 ); ++c ) { m_Name155[ c ] = (char)( 65 + ( ( c + 155 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id155 + a * 2 + b - (int)( m_Value155 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 186u ) ^ ( m_Flags1 << 12 ) ^ (unsigned)m_Id155; }
    bool Less( const Record155 & other ) const { return m_Id155 < other.m_Id155 || ( m_Id155 == other.m_Id155 && m_Value155 < other.m_Value155 ); }
};

template <class T> struct Box155
{
    T m_Value;
    T m_Other;
    Box155() : m_Value(), m_Other() {}
    explicit Box155( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box155 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box155<int> IntBox155;
typedef Box155<double> DoubleBox155;
inline int Total155( const IntBox155 & a, const DoubleBox155 & b, const Record155 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 155 ) + (int)r.Mix( 155u );
}
static const int g_Table155[] = { 155, 310, 466, 777, 1088, 1709 };

struct Record156
{
    int m_Id156;
    double m_Value156;
    char m_Name156[ 16 ];
    Record156 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record156() : m_Id156( 156 ), m_Value156( 156.5 ), m_Next( nullptr ), m_Label( "record156" ), m_Flags2( 468u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name156 ); ++c ) { m_Name156[ c ] = (char)( 65 + ( ( c + 156 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id156 + a * 3 + b - (int)( m_Value156 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 187u ) ^ ( m_Flags2 << 0 ) ^ (unsigned)m_Id156; }
    bool Less( const Record156 & other ) const { return m_Id156 < other.m_Id156 || ( m_Id156 == other.m_Id156 && m_Value156 < other.m_Value156 ); }
};

template <class T> struct Box156
{
    T m_Value;
    T m_Other;
    Box156() : m_Value(), m_Other() {}
    explicit Box156( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box156 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box156<int> IntBox156;
typedef Box156<double> DoubleBox156;
inline int Total156( const IntBox156 & a, const DoubleBox156 & b, const Record156 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 156 ) + (int)r.Mix( 156u );
}
static const int g_Table156[] = { 156, 312, 469, 782, 1095, 1720 };

struct Record157
{
    int m_Id157;
    double m_Value157;
    char m_Name157[ 20 ];
    Record157 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record157() : m_Id157( 157 ), m_Value157( 157.5 ), m_Next( nullptr ), m_Label( "record157" ), m_Flags3( 471u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name157 ); ++c ) { m_Name157[ c ] = (char)( 65 + ( ( c + 157 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id157 + a * 4 + b - (int)( m_Value157 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 188u ) ^ ( m_Flags3 << 1 ) ^ (unsigned)m_Id157; }
    bool Less( const Record157 & other ) const { return m_Id157 < other.m_Id157 || ( m_Id157 == other.m_Id157 && m_Value157 < other.m_Value157 ); }
};

template <class T> struct Box157
{
    T m_Value;
    T m_Other;
    Box157() : m_Value(), m_Other() {}
    explicit Box157( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box157 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box157<int> IntBox157;
typedef Box157<double> DoubleBox157;
inline int Total157( const IntBox157 & a, const DoubleBox157 & b, const Record157 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 157 ) + (int)r.Mix( 157u );
}
static const int g_Table157[] = { 157, 314, 472, 787, 1102, 1731 };

struct Record158
{
    int m_Id158;
    double m_Value158;
    char m_Name158[ 24 ];
    Record158 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record158() : m_Id158( 158 ), m_Value158( 158.5 ), m_Next( nullptr ), m_Label( "record158" ), m_Flags4( 474u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name158 ); ++c ) { m_Name158[ c ] = (char)( 65 + ( ( c + 158 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id158 + a * 5 + b - (int)( m_Value158 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 189u ) ^ ( m_Flags4 << 2 ) ^ (unsigned)m_Id158; }
    bool Less( const Record158 & other ) const { return m_Id158 < other.m_Id158 || ( m_Id158 == other.m_Id158 && m_Value158 < other.m_Value158 ); }
};

template <class T> struct Box158
{
    T m_Value;
    T m_Other;
    Box158() : m_Value(), m_Other() {}
    explicit Box158( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box158 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box158<int> IntBox158;
typedef Box158<double> DoubleBox158;
inline int Total158( const IntBox158 & a, const DoubleBox158 & b, const Record158 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 158 ) + (int)r.Mix( 158u );
}
static const int g_Table158[] = { 158, 316, 475, 792, 1109, 1742 };

struct Record159
{
    int m_Id159;
    double m_Value159;
    char m_Name159[ 28 ];
    Record159 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record159() : m_Id159( 159 ), m_Value159( 159.5 ), m_Next( nullptr ), m_Label( "record159" ), m_Flags5( 477u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name159 ); ++c ) { m_Name159[ c ] = (char)( 65 + ( ( c + 159 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id159 + a * 6 + b - (int)( m_Value159 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 190u ) ^ ( m_Flags5 << 3 ) ^ (unsigned)m_Id159; }
    bool Less( const Record159 & other ) const { return m_Id159 < other.m_Id159 || ( m_Id159 == other.m_Id159 && m_Value159 < other.m_Value159 ); }
};

template <class T> struct Box159
{
    T m_Value;
    T m_Other;
    Box159() : m_Value(), m_Other() {}
    explicit Box159( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box159 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box159<int> IntBox159;
typedef Box159<double> DoubleBox159;
inline int Total159( const IntBox159 & a, const DoubleBox159 & b, const Record159 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 159 ) + (int)r.Mix( 159u );
}
static const int g_Table159[] = { 159, 318, 478, 797, 1116, 1753 };

struct Record160
{
    int m_Id160;
    double m_Value160;
    char m_Name160[ 12 ];
    Record160 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record160() : m_Id160( 160 ), m_Value160( 160.5 ), m_Next( nullptr ), m_Label( "record160" ), m_Flags6( 480u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name160 ); ++c ) { m_Name160[ c ] = (char)( 65 + ( ( c + 160 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id160 + a * 7 + b - (int)( m_Value160 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 191u ) ^ ( m_Flags6 << 4 ) ^ (unsigned)m_Id160; }
    bool Less( const Record160 & other ) const { return m_Id160 < other.m_Id160 || ( m_Id160 == other.m_Id160 && m_Value160 < other.m_Value160 ); }
};

template <class T> struct Box160
{
    T m_Value;
    T m_Other;
    Box160() : m_Value(), m_Other() {}
    explicit Box160( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box160 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box160<int> IntBox160;
typedef Box160<double> DoubleBox160;
inline int Total160( const IntBox160 & a, const DoubleBox160 & b, const Record160 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 160 ) + (int)r.Mix( 160u );
}
static const int g_Table160[] = { 160, 320, 481, 802, 1123, 1764 };

struct Record161
{
    int m_Id161;
    double m_Value161;
    char m_Name161[ 16 ];
    Record161 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record161() : m_Id161( 161 ), m_Value161( 161.5 ), m_Next( nullptr ), m_Label( "record161" ), m_Flags7( 483u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name161 ); ++c ) { m_Name161[ c ] = (char)( 65 + ( ( c + 161 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id161 + a * 1 + b - (int)( m_Value161 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 192u ) ^ ( m_Flags7 << 5 ) ^ (unsigned)m_Id161; }
    bool Less( const Record161 & other ) const { return m_Id161 < other.m_Id161 || ( m_Id161 == other.m_Id161 && m_Value161 < other.m_Value161 ); }
};

template <class T> struct Box161
{
    T m_Value;
    T m_Other;
    Box161() : m_Value(), m_Other() {}
    explicit Box161( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box161 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box161<int> IntBox161;
typedef Box161<double> DoubleBox161;
inline int Total161( const IntBox161 & a, const DoubleBox161 & b, const Record161 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 161 ) + (int)r.Mix( 161u );
}
static const int g_Table161[] = { 161, 322, 484, 807, 1130, 1775 };

struct Record162
{
    int m_Id162;
    double m_Value162;
    char m_Name162[ 20 ];
    Record162 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record162() : m_Id162( 162 ), m_Value162( 162.5 ), m_Next( nullptr ), m_Label( "record162" ), m_Flags8( 486u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name162 ); ++c ) { m_Name162[ c ] = (char)( 65 + ( ( c + 162 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id162 + a * 2 + b - (int)( m_Value162 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 193u ) ^ ( m_Flags8 << 6 ) ^ (unsigned)m_Id162; }
    bool Less( const Record162 & other ) const { return m_Id162 < other.m_Id162 || ( m_Id162 == other.m_Id162 && m_Value162 < other.m_Value162 ); }
};

template <class T> struct Box162
{
    T m_Value;
    T m_Other;
    Box162() : m_Value(), m_Other() {}
    explicit Box162( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box162 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box162<int> IntBox162;
typedef Box162<double> DoubleBox162;
inline int Total162( const IntBox162 & a, const DoubleBox162 & b, const Record162 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 162 ) + (int)r.Mix( 162u );
}
static const int g_Table162[] = { 162, 324, 487, 812, 1137, 1786 };

struct Record163
{
    int m_Id163;
    double m_Value163;
    char m_Name163[ 24 ];
    Record163 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record163() : m_Id163( 163 ), m_Value163( 163.5 ), m_Next( nullptr ), m_Label( "record163" ), m_Flags9( 489u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name163 ); ++c ) { m_Name163[ c ] = (char)( 65 + ( ( c + 163 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id163 + a * 3 + b - (int)( m_Value163 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 194u ) ^ ( m_Flags9 << 7 ) ^ (unsigned)m_Id163; }
    bool Less( const Record163 & other ) const { return m_Id163 < other.m_Id163 || ( m_Id163 == other.m_Id163 && m_Value163 < other.m_Value163 ); }
};

template <class T> struct Box163
{
    T m_Value;
    T m_Other;
    Box163() : m_Value(), m_Other() {}
    explicit Box163( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box163 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box163<int> IntBox163;
typedef Box163<double> DoubleBox163;
inline int Total163( const IntBox163 & a, const DoubleBox163 & b, const Record163 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 163 ) + (int)r.Mix( 163u );
}
static const int g_Table163[] = { 163, 326, 490, 817, 1144, 1797 };

struct Record164
{
    int m_Id164;
    double m_Value164;
    char m_Name164[ 28 ];
    Record164 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record164() : m_Id164( 164 ), m_Value164( 164.5 ), m_Next( nullptr ), m_Label( "record164" ), m_Flags10( 492u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name164 ); ++c ) { m_Name164[ c ] = (char)( 65 + ( ( c + 164 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id164 + a * 4 + b - (int)( m_Value164 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 195u ) ^ ( m_Flags10 << 8 ) ^ (unsigned)m_Id164; }
    bool Less( const Record164 & other ) const { return m_Id164 < other.m_Id164 || ( m_Id164 == other.m_Id164 && m_Value164 < other.m_Value164 ); }
};

template <class T> struct Box164
{
    T m_Value;
    T m_Other;
    Box164() : m_Value(), m_Other() {}
    explicit Box164( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box164 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box164<int> IntBox164;
typedef Box164<double> DoubleBox164;
inline int Total164( const IntBox164 & a, const DoubleBox164 & b, const Record164 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 164 ) + (int)r.Mix( 164u );
}
static const int g_Table164[] = { 164, 328, 493, 822, 1151, 1808 };

struct Record165
{
    int m_Id165;
    double m_Value165;
    char m_Name165[ 12 ];
    Record165 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record165() : m_Id165( 165 ), m_Value165( 165.5 ), m_Next( nullptr ), m_Label( "record165" ), m_Flags0( 495u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name165 ); ++c ) { m_Name165[ c ] = (char)( 65 + ( ( c + 165 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id165 + a * 5 + b - (int)( m_Value165 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 196u ) ^ ( m_Flags0 << 9 ) ^ (unsigned)m_Id165; }
    bool Less( const Record165 & other ) const { return m_Id165 < other.m_Id165 || ( m_Id165 == other.m_Id165 && m_Value165 < other.m_Value165 ); }
};

template <class T> struct Box165
{
    T m_Value;
    T m_Other;
    Box165() : m_Value(), m_Other() {}
    explicit Box165( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box165 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box165<int> IntBox165;
typedef Box165<double> DoubleBox165;
inline int Total165( const IntBox165 & a, const DoubleBox165 & b, const Record165 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 165 ) + (int)r.Mix( 165u );
}
static const int g_Table165[] = { 165, 330, 496, 827, 1158, 1819 };

struct Record166
{
    int m_Id166;
    double m_Value166;
    char m_Name166[ 16 ];
    Record166 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record166() : m_Id166( 166 ), m_Value166( 166.5 ), m_Next( nullptr ), m_Label( "record166" ), m_Flags1( 498u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name166 ); ++c ) { m_Name166[ c ] = (char)( 65 + ( ( c + 166 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id166 + a * 6 + b - (int)( m_Value166 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 197u ) ^ ( m_Flags1 << 10 ) ^ (unsigned)m_Id166; }
    bool Less( const Record166 & other ) const { return m_Id166 < other.m_Id166 || ( m_Id166 == other.m_Id166 && m_Value166 < other.m_Value166 ); }
};

template <class T> struct Box166
{
    T m_Value;
    T m_Other;
    Box166() : m_Value(), m_Other() {}
    explicit Box166( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box166 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box166<int> IntBox166;
typedef Box166<double> DoubleBox166;
inline int Total166( const IntBox166 & a, const DoubleBox166 & b, const Record166 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 166 ) + (int)r.Mix( 166u );
}
static const int g_Table166[] = { 166, 332, 499, 832, 1165, 1830 };

struct Record167
{
    int m_Id167;
    double m_Value167;
    char m_Name167[ 20 ];
    Record167 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record167() : m_Id167( 167 ), m_Value167( 167.5 ), m_Next( nullptr ), m_Label( "record167" ), m_Flags2( 501u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name167 ); ++c ) { m_Name167[ c ] = (char)( 65 + ( ( c + 167 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id167 + a * 7 + b - (int)( m_Value167 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 198u ) ^ ( m_Flags2 << 11 ) ^ (unsigned)m_Id167; }
    bool Less( const Record167 & other ) const { return m_Id167 < other.m_Id167 || ( m_Id167 == other.m_Id167 && m_Value167 < other.m_Value167 ); }
};

template <class T> struct Box167
{
    T m_Value;
    T m_Other;
    Box167() : m_Value(), m_Other() {}
    explicit Box167( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box167 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box167<int> IntBox167;
typedef Box167<double> DoubleBox167;
inline int Total167( const IntBox167 & a, const DoubleBox167 & b, const Record167 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 167 ) + (int)r.Mix( 167u );
}
static const int g_Table167[] = { 167, 334, 502, 837, 1172, 1841 };

struct Record168
{
    int m_Id168;
    double m_Value168;
    char m_Name168[ 24 ];
    Record168 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record168() : m_Id168( 168 ), m_Value168( 168.5 ), m_Next( nullptr ), m_Label( "record168" ), m_Flags3( 504u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name168 ); ++c ) { m_Name168[ c ] = (char)( 65 + ( ( c + 168 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id168 + a * 1 + b - (int)( m_Value168 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 199u ) ^ ( m_Flags3 << 12 ) ^ (unsigned)m_Id168; }
    bool Less( const Record168 & other ) const { return m_Id168 < other.m_Id168 || ( m_Id168 == other.m_Id168 && m_Value168 < other.m_Value168 ); }
};

template <class T> struct Box168
{
    T m_Value;
    T m_Other;
    Box168() : m_Value(), m_Other() {}
    explicit Box168( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box168 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box168<int> IntBox168;
typedef Box168<double> DoubleBox168;
inline int Total168( const IntBox168 & a, const DoubleBox168 & b, const Record168 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 168 ) + (int)r.Mix( 168u );
}
static const int g_Table168[] = { 168, 336, 505, 842, 1179, 1852 };

struct Record169
{
    int m_Id169;
    double m_Value169;
    char m_Name169[ 28 ];
    Record169 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record169() : m_Id169( 169 ), m_Value169( 169.5 ), m_Next( nullptr ), m_Label( "record169" ), m_Flags4( 507u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name169 ); ++c ) { m_Name169[ c ] = (char)( 65 + ( ( c + 169 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id169 + a * 2 + b - (int)( m_Value169 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 200u ) ^ ( m_Flags4 << 0 ) ^ (unsigned)m_Id169; }
    bool Less( const Record169 & other ) const { return m_Id169 < other.m_Id169 || ( m_Id169 == other.m_Id169 && m_Value169 < other.m_Value169 ); }
};

template <class T> struct Box169
{
    T m_Value;
    T m_Other;
    Box169() : m_Value(), m_Other() {}
    explicit Box169( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box169 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box169<int> IntBox169;
typedef Box169<double> DoubleBox169;
inline int Total169( const IntBox169 & a, const DoubleBox169 & b, const Record169 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 169 ) + (int)r.Mix( 169u );
}
static const int g_Table169[] = { 169, 338, 508, 847, 1186, 1863 };

struct Record170
{
    int m_Id170;
    double m_Value170;
    char m_Name170[ 12 ];
    Record170 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record170() : m_Id170( 170 ), m_Value170( 170.5 ), m_Next( nullptr ), m_Label( "record170" ), m_Flags5( 510u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name170 ); ++c ) { m_Name170[ c ] = (char)( 65 + ( ( c + 170 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id170 + a * 3 + b - (int)( m_Value170 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 201u ) ^ ( m_Flags5 << 1 ) ^ (unsigned)m_Id170; }
    bool Less( const Record170 & other ) const { return m_Id170 < other.m_Id170 || ( m_Id170 == other.m_Id170 && m_Value170 < other.m_Value170 ); }
};

template <class T> struct Box170
{
    T m_Value;
    T m_Other;
    Box170() : m_Value(), m_Other() {}
    explicit Box170( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box170 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box170<int> IntBox170;
typedef Box170<double> DoubleBox170;
inline int Total170( const IntBox170 & a, const DoubleBox170 & b, const Record170 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 170 ) + (int)r.Mix( 170u );
}
static const int g_Table170[] = { 170, 340, 511, 852, 1193, 1874 };

struct Record171
{
    int m_Id171;
    double m_Value171;
    char m_Name171[ 16 ];
    Record171 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record171() : m_Id171( 171 ), m_Value171( 171.5 ), m_Next( nullptr ), m_Label( "record171" ), m_Flags6( 513u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name171 ); ++c ) { m_Name171[ c ] = (char)( 65 + ( ( c + 171 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id171 + a * 4 + b - (int)( m_Value171 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 202u ) ^ ( m_Flags6 << 2 ) ^ (unsigned)m_Id171; }
    bool Less( const Record171 & other ) const { return m_Id171 < other.m_Id171 || ( m_Id171 == other.m_Id171 && m_Value171 < other.m_Value171 ); }
};

template <class T> struct Box171
{
    T m_Value;
    T m_Other;
    Box171() : m_Value(), m_Other() {}
    explicit Box171( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box171 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box171<int> IntBox171;
typedef Box171<double> DoubleBox171;
inline int Total171( const IntBox171 & a, const DoubleBox171 & b, const Record171 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 171 ) + (int)r.Mix( 171u );
}
static const int g_Table171[] = { 171, 342, 514, 857, 1200, 1885 };

struct Record172
{
    int m_Id172;
    double m_Value172;
    char m_Name172[ 20 ];
    Record172 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record172() : m_Id172( 172 ), m_Value172( 172.5 ), m_Next( nullptr ), m_Label( "record172" ), m_Flags7( 516u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name172 ); ++c ) { m_Name172[ c ] = (char)( 65 + ( ( c + 172 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id172 + a * 5 + b - (int)( m_Value172 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 203u ) ^ ( m_Flags7 << 3 ) ^ (unsigned)m_Id172; }
    bool Less( const Record172 & other ) const { return m_Id172 < other.m_Id172 || ( m_Id172 == other.m_Id172 && m_Value172 < other.m_Value172 ); }
};

template <class T> struct Box172
{
    T m_Value;
    T m_Other;
    Box172() : m_Value(), m_Other() {}
    explicit Box172( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box172 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box172<int> IntBox172;
typedef Box172<double> DoubleBox172;
inline int Total172( const IntBox172 & a, const DoubleBox172 & b, const Record172 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 172 ) + (int)r.Mix( 172u );
}
static const int g_Table172[] = { 172, 344, 517, 862, 1207, 1896 };

struct Record173
{
    int m_Id173;
    double m_Value173;
    char m_Name173[ 24 ];
    Record173 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record173() : m_Id173( 173 ), m_Value173( 173.5 ), m_Next( nullptr ), m_Label( "record173" ), m_Flags8( 519u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name173 ); ++c ) { m_Name173[ c ] = (char)( 65 + ( ( c + 173 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id173 + a * 6 + b - (int)( m_Value173 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 204u ) ^ ( m_Flags8 << 4 ) ^ (unsigned)m_Id173; }
    bool Less( const Record173 & other ) const { return m_Id173 < other.m_Id173 || ( m_Id173 == other.m_Id173 && m_Value173 < other.m_Value173 ); }
};

template <class T> struct Box173
{
    T m_Value;
    T m_Other;
    Box173() : m_Value(), m_Other() {}
    explicit Box173( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box173 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box173<int> IntBox173;
typedef Box173<double> DoubleBox173;
inline int Total173( const IntBox173 & a, const DoubleBox173 & b, const Record173 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 173 ) + (int)r.Mix( 173u );
}
static const int g_Table173[] = { 173, 346, 520, 867, 1214, 1907 };

struct Record174
{
    int m_Id174;
    double m_Value174;
    char m_Name174[ 28 ];
    Record174 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record174() : m_Id174( 174 ), m_Value174( 174.5 ), m_Next( nullptr ), m_Label( "record174" ), m_Flags9( 522u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name174 ); ++c ) { m_Name174[ c ] = (char)( 65 + ( ( c + 174 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id174 + a * 7 + b - (int)( m_Value174 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 205u ) ^ ( m_Flags9 << 5 ) ^ (unsigned)m_Id174; }
    bool Less( const Record174 & other ) const { return m_Id174 < other.m_Id174 || ( m_Id174 == other.m_Id174 && m_Value174 < other.m_Value174 ); }
};

template <class T> struct Box174
{
    T m_Value;
    T m_Other;
    Box174() : m_Value(), m_Other() {}
    explicit Box174( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box174 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box174<int> IntBox174;
typedef Box174<double> DoubleBox174;
inline int Total174( const IntBox174 & a, const DoubleBox174 & b, const Record174 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 174 ) + (int)r.Mix( 174u );
}
static const int g_Table174[] = { 174, 348, 523, 872, 1221, 1918 };

struct Record175
{
    int m_Id175;
    double m_Value175;
    char m_Name175[ 12 ];
    Record175 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record175() : m_Id175( 175 ), m_Value175( 175.5 ), m_Next( nullptr ), m_Label( "record175" ), m_Flags10( 525u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name175 ); ++c ) { m_Name175[ c ] = (char)( 65 + ( ( c + 175 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id175 + a * 1 + b - (int)( m_Value175 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 206u ) ^ ( m_Flags10 << 6 ) ^ (unsigned)m_Id175; }
    bool Less( const Record175 & other ) const { return m_Id175 < other.m_Id175 || ( m_Id175 == other.m_Id175 && m_Value175 < other.m_Value175 ); }
};

template <class T> struct Box175
{
    T m_Value;
    T m_Other;
    Box175() : m_Value(), m_Other() {}
    explicit Box175( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box175 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box175<int> IntBox175;
typedef Box175<double> DoubleBox175;
inline int Total175( const IntBox175 & a, const DoubleBox175 & b, const Record175 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 175 ) + (int)r.Mix( 175u );
}
static const int g_Table175[] = { 175, 350, 526, 877, 1228, 1929 };

struct Record176
{
    int m_Id176;
    double m_Value176;
    char m_Name176[ 16 ];
    Record176 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record176() : m_Id176( 176 ), m_Value176( 176.5 ), m_Next( nullptr ), m_Label( "record176" ), m_Flags0( 528u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name176 ); ++c ) { m_Name176[ c ] = (char)( 65 + ( ( c + 176 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id176 + a * 2 + b - (int)( m_Value176 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 207u ) ^ ( m_Flags0 << 7 ) ^ (unsigned)m_Id176; }
    bool Less( const Record176 & other ) const { return m_Id176 < other.m_Id176 || ( m_Id176 == other.m_Id176 && m_Value176 < other.m_Value176 ); }
};

template <class T> struct Box176
{
    T m_Value;
    T m_Other;
    Box176() : m_Value(), m_Other() {}
    explicit Box176( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box176 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box176<int> IntBox176;
typedef Box176<double> DoubleBox176;
inline int Total176( const IntBox176 & a, const DoubleBox176 & b, const Record176 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 176 ) + (int)r.Mix( 176u );
}
static const int g_Table176[] = { 176, 352, 529, 882, 1235, 1940 };

struct Record177
{
    int m_Id177;
    double m_Value177;
    char m_Name177[ 20 ];
    Record177 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record177() : m_Id177( 177 ), m_Value177( 177.5 ), m_Next( nullptr ), m_Label( "record177" ), m_Flags1( 531u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name177 ); ++c ) { m_Name177[ c ] = (char)( 65 + ( ( c + 177 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id177 + a * 3 + b - (int)( m_Value177 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 208u ) ^ ( m_Flags1 << 8 ) ^ (unsigned)m_Id177; }
    bool Less( const Record177 & other ) const { return m_Id177 < other.m_Id177 || ( m_Id177 == other.m_Id177 && m_Value177 < other.m_Value177 ); }
};

template <class T> struct Box177
{
    T m_Value;
    T m_Other;
    Box177() : m_Value(), m_Other() {}
    explicit Box177( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box177 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box177<int> IntBox177;
typedef Box177<double> DoubleBox177;
inline int Total177( const IntBox177 & a, const DoubleBox177 & b, const Record177 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 177 ) + (int)r.Mix( 177u );
}
static const int g_Table177[] = { 177, 354, 532, 887, 1242, 1951 };

struct Record178
{
    int m_Id178;
    double m_Value178;
    char m_Name178[ 24 ];
    Record178 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record178() : m_Id178( 178 ), m_Value178( 178.5 ), m_Next( nullptr ), m_Label( "record178" ), m_Flags2( 534u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name178 ); ++c ) { m_Name178[ c ] = (char)( 65 + ( ( c + 178 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id178 + a * 4 + b - (int)( m_Value178 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 209u ) ^ ( m_Flags2 << 9 ) ^ (unsigned)m_Id178; }
    bool Less( const Record178 & other ) const { return m_Id178 < other.m_Id178 || ( m_Id178 == other.m_Id178 && m_Value178 < other.m_Value178 ); }
};

template <class T> struct Box178
{
    T m_Value;
    T m_Other;
    Box178() : m_Value(), m_Other() {}
    explicit Box178( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box178 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box178<int> IntBox178;
typedef Box178<double> DoubleBox178;
inline int Total178( const IntBox178 & a, const DoubleBox178 & b, const Record178 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 178 ) + (int)r.Mix( 178u );
}
static const int g_Table178[] = { 178, 356, 535, 892, 1249, 1962 };

struct Record179
{
    int m_Id179;
    double m_Value179;
    char m_Name179[ 28 ];
    Record179 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record179() : m_Id179( 179 ), m_Value179( 179.5 ), m_Next( nullptr ), m_Label( "record179" ), m_Flags3( 537u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name179 ); ++c ) { m_Name179[ c ] = (char)( 65 + ( ( c + 179 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id179 + a * 5 + b - (int)( m_Value179 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 210u ) ^ ( m_Flags3 << 10 ) ^ (unsigned)m_Id179; }
    bool Less( const Record179 & other ) const { return m_Id179 < other.m_Id179 || ( m_Id179 == other.m_Id179 && m_Value179 < other.m_Value179 ); }
};

template <class T> struct Box179
{
    T m_Value;
    T m_Other;
    Box179() : m_Value(), m_Other() {}
    explicit Box179( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box179 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box179<int> IntBox179;
typedef Box179<double> DoubleBox179;
inline int Total179( const IntBox179 & a, const DoubleBox179 & b, const Record179 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 179 ) + (int)r.Mix( 179u );
}
static const int g_Table179[] = { 179, 358, 538, 897, 1256, 1973 };

struct Record180
{
    int m_Id180;
    double m_Value180;
    char m_Name180[ 12 ];
    Record180 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record180() : m_Id180( 180 ), m_Value180( 180.5 ), m_Next( nullptr ), m_Label( "record180" ), m_Flags4( 540u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name180 ); ++c ) { m_Name180[ c ] = (char)( 65 + ( ( c + 180 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id180 + a * 6 + b - (int)( m_Value180 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 211u ) ^ ( m_Flags4 << 11 ) ^ (unsigned)m_Id180; }
    bool Less( const Record180 & other ) const { return m_Id180 < other.m_Id180 || ( m_Id180 == other.m_Id180 && m_Value180 < other.m_Value180 ); }
};

template <class T> struct Box180
{
    T m_Value;
    T m_Other;
    Box180() : m_Value(), m_Other() {}
    explicit Box180( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box180 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box180<int> IntBox180;
typedef Box180<double> DoubleBox180;
inline int Total180( const IntBox180 & a, const DoubleBox180 & b, const Record180 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 180 ) + (int)r.Mix( 180u );
}
static const int g_Table180[] = { 180, 360, 541, 902, 1263, 1984 };

struct Record181
{
    int m_Id181;
    double m_Value181;
    char m_Name181[ 16 ];
    Record181 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record181() : m_Id181( 181 ), m_Value181( 181.5 ), m_Next( nullptr ), m_Label( "record181" ), m_Flags5( 543u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name181 ); ++c ) { m_Name181[ c ] = (char)( 65 + ( ( c + 181 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id181 + a * 7 + b - (int)( m_Value181 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 212u ) ^ ( m_Flags5 << 12 ) ^ (unsigned)m_Id181; }
    bool Less( const Record181 & other ) const { return m_Id181 < other.m_Id181 || ( m_Id181 == other.m_Id181 && m_Value181 < other.m_Value181 ); }
};

template <class T> struct Box181
{
    T m_Value;
    T m_Other;
    Box181() : m_Value(), m_Other() {}
    explicit Box181( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box181 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box181<int> IntBox181;
typedef Box181<double> DoubleBox181;
inline int Total181( const IntBox181 & a, const DoubleBox181 & b, const Record181 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 181 ) + (int)r.Mix( 181u );
}
static const int g_Table181[] = { 181, 362, 544, 907, 1270, 1995 };

struct Record182
{
    int m_Id182;
    double m_Value182;
    char m_Name182[ 20 ];
    Record182 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record182() : m_Id182( 182 ), m_Value182( 182.5 ), m_Next( nullptr ), m_Label( "record182" ), m_Flags6( 546u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name182 ); ++c ) { m_Name182[ c ] = (char)( 65 + ( ( c + 182 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id182 + a * 1 + b - (int)( m_Value182 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 213u ) ^ ( m_Flags6 << 0 ) ^ (unsigned)m_Id182; }
    bool Less( const Record182 & other ) const { return m_Id182 < other.m_Id182 || ( m_Id182 == other.m_Id182 && m_Value182 < other.m_Value182 ); }
};

template <class T> struct Box182
{
    T m_Value;
    T m_Other;
    Box182() : m_Value(), m_Other() {}
    explicit Box182( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box182 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box182<int> IntBox182;
typedef Box182<double> DoubleBox182;
inline int Total182( const IntBox182 & a, const DoubleBox182 & b, const Record182 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 182 ) + (int)r.Mix( 182u );
}
static const int g_Table182[] = { 182, 364, 547, 912, 1277, 2006 };

struct Record183
{
    int m_Id183;
    double m_Value183;
    char m_Name183[ 24 ];
    Record183 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record183() : m_Id183( 183 ), m_Value183( 183.5 ), m_Next( nullptr ), m_Label( "record183" ), m_Flags7( 549u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name183 ); ++c ) { m_Name183[ c ] = (char)( 65 + ( ( c + 183 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id183 + a * 2 + b - (int)( m_Value183 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 214u ) ^ ( m_Flags7 << 1 ) ^ (unsigned)m_Id183; }
    bool Less( const Record183 & other ) const { return m_Id183 < other.m_Id183 || ( m_Id183 == other.m_Id183 && m_Value183 < other.m_Value183 ); }
};

template <class T> struct Box183
{
    T m_Value;
    T m_Other;
    Box183() : m_Value(), m_Other() {}
    explicit Box183( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box183 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box183<int> IntBox183;
typedef Box183<double> DoubleBox183;
inline int Total183( const IntBox183 & a, const DoubleBox183 & b, const Record183 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 183 ) + (int)r.Mix( 183u );
}
static const int g_Table183[] = { 183, 366, 550, 917, 1284, 2017 };

struct Record184
{
    int m_Id184;
    double m_Value184;
    char m_Name184[ 28 ];
    Record184 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record184() : m_Id184( 184 ), m_Value184( 184.5 ), m_Next( nullptr ), m_Label( "record184" ), m_Flags8( 552u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name184 ); ++c ) { m_Name184[ c ] = (char)( 65 + ( ( c + 184 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id184 + a * 3 + b - (int)( m_Value184 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 215u ) ^ ( m_Flags8 << 2 ) ^ (unsigned)m_Id184; }
    bool Less( const Record184 & other ) const { return m_Id184 < other.m_Id184 || ( m_Id184 == other.m_Id184 && m_Value184 < other.m_Value184 ); }
};

template <class T> struct Box184
{
    T m_Value;
    T m_Other;
    Box184() : m_Value(), m_Other() {}
    explicit Box184( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box184 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box184<int> IntBox184;
typedef Box184<double> DoubleBox184;
inline int Total184( const IntBox184 & a, const DoubleBox184 & b, const Record184 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 184 ) + (int)r.Mix( 184u );
}
static const int g_Table184[] = { 184, 368, 553, 922, 1291, 2028 };

struct Record185
{
    int m_Id185;
    double m_Value185;
    char m_Name185[ 12 ];
    Record185 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record185() : m_Id185( 185 ), m_Value185( 185.5 ), m_Next( nullptr ), m_Label( "record185" ), m_Flags9( 555u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name185 ); ++c ) { m_Name185[ c ] = (char)( 65 + ( ( c + 185 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id185 + a * 4 + b - (int)( m_Value185 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 216u ) ^ ( m_Flags9 << 3 ) ^ (unsigned)m_Id185; }
    bool Less( const Record185 & other ) const { return m_Id185 < other.m_Id185 || ( m_Id185 == other.m_Id185 && m_Value185 < other.m_Value185 ); }
};

template <class T> struct Box185
{
    T m_Value;
    T m_Other;
    Box185() : m_Value(), m_Other() {}
    explicit Box185( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box185 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box185<int> IntBox185;
typedef Box185<double> DoubleBox185;
inline int Total185( const IntBox185 & a, const DoubleBox185 & b, const Record185 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 185 ) + (int)r.Mix( 185u );
}
static const int g_Table185[] = { 185, 370, 556, 927, 1298, 2039 };

struct Record186
{
    int m_Id186;
    double m_Value186;
    char m_Name186[ 16 ];
    Record186 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record186() : m_Id186( 186 ), m_Value186( 186.5 ), m_Next( nullptr ), m_Label( "record186" ), m_Flags10( 558u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name186 ); ++c ) { m_Name186[ c ] = (char)( 65 + ( ( c + 186 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id186 + a * 5 + b - (int)( m_Value186 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 217u ) ^ ( m_Flags10 << 4 ) ^ (unsigned)m_Id186; }
    bool Less( const Record186 & other ) const { return m_Id186 < other.m_Id186 || ( m_Id186 == other.m_Id186 && m_Value186 < other.m_Value186 ); }
};

template <class T> struct Box186
{
    T m_Value;
    T m_Other;
    Box186() : m_Value(), m_Other() {}
    explicit Box186( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box186 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box186<int> IntBox186;
typedef Box186<double> DoubleBox186;
inline int Total186( const IntBox186 & a, const DoubleBox186 & b, const Record186 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 186 ) + (int)r.Mix( 186u );
}
static const int g_Table186[] = { 186, 372, 559, 932, 1305, 2050 };

struct Record187
{
    int m_Id187;
    double m_Value187;
    char m_Name187[ 20 ];
    Record187 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record187() : m_Id187( 187 ), m_Value187( 187.5 ), m_Next( nullptr ), m_Label( "record187" ), m_Flags0( 561u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name187 ); ++c ) { m_Name187[ c ] = (char)( 65 + ( ( c + 187 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id187 + a * 6 + b - (int)( m_Value187 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 218u ) ^ ( m_Flags0 << 5 ) ^ (unsigned)m_Id187; }
    bool Less( const Record187 & other ) const { return m_Id187 < other.m_Id187 || ( m_Id187 == other.m_Id187 && m_Value187 < other.m_Value187 ); }
};

template <class T> struct Box187
{
    T m_Value;
    T m_Other;
    Box187() : m_Value(), m_Other() {}
    explicit Box187( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box187 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box187<int> IntBox187;
typedef Box187<double> DoubleBox187;
inline int Total187( const IntBox187 & a, const DoubleBox187 & b, const Record187 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 187 ) + (int)r.Mix( 187u );
}
static const int g_Table187[] = { 187, 374, 562, 937, 1312, 2061 };

struct Record188
{
    int m_Id188;
    double m_Value188;
    char m_Name188[ 24 ];
    Record188 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record188() : m_Id188( 188 ), m_Value188( 188.5 ), m_Next( nullptr ), m_Label( "record188" ), m_Flags1( 564u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name188 ); ++c ) { m_Name188[ c ] = (char)( 65 + ( ( c + 188 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id188 + a * 7 + b - (int)( m_Value188 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 219u ) ^ ( m_Flags1 << 6 ) ^ (unsigned)m_Id188; }
    bool Less( const Record188 & other ) const { return m_Id188 < other.m_Id188 || ( m_Id188 == other.m_Id188 && m_Value188 < other.m_Value188 ); }
};

template <class T> struct Box188
{
    T m_Value;
    T m_Other;
    Box188() : m_Value(), m_Other() {}
    explicit Box188( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box188 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box188<int> IntBox188;
typedef Box188<double> DoubleBox188;
inline int Total188( const IntBox188 & a, const DoubleBox188 & b, const Record188 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 188 ) + (int)r.Mix( 188u );
}
static const int g_Table188[] = { 188, 376, 565, 942, 1319, 2072 };

struct Record189
{
    int m_Id189;
    double m_Value189;
    char m_Name189[ 28 ];
    Record189 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record189() : m_Id189( 189 ), m_Value189( 189.5 ), m_Next( nullptr ), m_Label( "record189" ), m_Flags2( 567u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name189 ); ++c ) { m_Name189[ c ] = (char)( 65 + ( ( c + 189 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id189 + a * 1 + b - (int)( m_Value189 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 220u ) ^ ( m_Flags2 << 7 ) ^ (unsigned)m_Id189; }
    bool Less( const Record189 & other ) const { return m_Id189 < other.m_Id189 || ( m_Id189 == other.m_Id189 && m_Value189 < other.m_Value189 ); }
};

template <class T> struct Box189
{
    T m_Value;
    T m_Other;
    Box189() : m_Value(), m_Other() {}
    explicit Box189( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box189 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box189<int> IntBox189;
typedef Box189<double> DoubleBox189;
inline int Total189( const IntBox189 & a, const DoubleBox189 & b, const Record189 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 189 ) + (int)r.Mix( 189u );
}
static const int g_Table189[] = { 189, 378, 568, 947, 1326, 2083 };

struct Record190
{
    int m_Id190;
    double m_Value190;
    char m_Name190[ 12 ];
    Record190 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record190() : m_Id190( 190 ), m_Value190( 190.5 ), m_Next( nullptr ), m_Label( "record190" ), m_Flags3( 570u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name190 ); ++c ) { m_Name190[ c ] = (char)( 65 + ( ( c + 190 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id190 + a * 2 + b - (int)( m_Value190 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 221u ) ^ ( m_Flags3 << 8 ) ^ (unsigned)m_Id190; }
    bool Less( const Record190 & other ) const { return m_Id190 < other.m_Id190 || ( m_Id190 == other.m_Id190 && m_Value190 < other.m_Value190 ); }
};

template <class T> struct Box190
{
    T m_Value;
    T m_Other;
    Box190() : m_Value(), m_Other() {}
    explicit Box190( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box190 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box190<int> IntBox190;
typedef Box190<double> DoubleBox190;
inline int Total190( const IntBox190 & a, const DoubleBox190 & b, const Record190 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 190 ) + (int)r.Mix( 190u );
}
static const int g_Table190[] = { 190, 380, 571, 952, 1333, 2094 };

struct Record191
{
    int m_Id191;
    double m_Value191;
    char m_Name191[ 16 ];
    Record191 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record191() : m_Id191( 191 ), m_Value191( 191.5 ), m_Next( nullptr ), m_Label( "record191" ), m_Flags4( 573u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name191 ); ++c ) { m_Name191[ c ] = (char)( 65 + ( ( c + 191 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id191 + a * 3 + b - (int)( m_Value191 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 222u ) ^ ( m_Flags4 << 9 ) ^ (unsigned)m_Id191; }
    bool Less( const Record191 & other ) const { return m_Id191 < other.m_Id191 || ( m_Id191 == other.m_Id191 && m_Value191 < other.m_Value191 ); }
};

template <class T> struct Box191
{
    T m_Value;
    T m_Other;
    Box191() : m_Value(), m_Other() {}
    explicit Box191( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box191 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box191<int> IntBox191;
typedef Box191<double> DoubleBox191;
inline int Total191( const IntBox191 & a, const DoubleBox191 & b, const Record191 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 191 ) + (int)r.Mix( 191u );
}
static const int g_Table191[] = { 191, 382, 574, 957, 1340, 2105 };

struct Record192
{
    int m_Id192;
    double m_Value192;
    char m_Name192[ 20 ];
    Record192 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record192() : m_Id192( 192 ), m_Value192( 192.5 ), m_Next( nullptr ), m_Label( "record192" ), m_Flags5( 576u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name192 ); ++c ) { m_Name192[ c ] = (char)( 65 + ( ( c + 192 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id192 + a * 4 + b - (int)( m_Value192 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 223u ) ^ ( m_Flags5 << 10 ) ^ (unsigned)m_Id192; }
    bool Less( const Record192 & other ) const { return m_Id192 < other.m_Id192 || ( m_Id192 == other.m_Id192 && m_Value192 < other.m_Value192 ); }
};

template <class T> struct Box192
{
    T m_Value;
    T m_Other;
    Box192() : m_Value(), m_Other() {}
    explicit Box192( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box192 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box192<int> IntBox192;
typedef Box192<double> DoubleBox192;
inline int Total192( const IntBox192 & a, const DoubleBox192 & b, const Record192 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 192 ) + (int)r.Mix( 192u );
}
static const int g_Table192[] = { 192, 384, 577, 962, 1347, 2116 };

struct Record193
{
    int m_Id193;
    double m_Value193;
    char m_Name193[ 24 ];
    Record193 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record193() : m_Id193( 193 ), m_Value193( 193.5 ), m_Next( nullptr ), m_Label( "record193" ), m_Flags6( 579u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name193 ); ++c ) { m_Name193[ c ] = (char)( 65 + ( ( c + 193 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id193 + a * 5 + b - (int)( m_Value193 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 224u ) ^ ( m_Flags6 << 11 ) ^ (unsigned)m_Id193; }
    bool Less( const Record193 & other ) const { return m_Id193 < other.m_Id193 || ( m_Id193 == other.m_Id193 && m_Value193 < other.m_Value193 ); }
};

template <class T> struct Box193
{
    T m_Value;
    T m_Other;
    Box193() : m_Value(), m_Other() {}
    explicit Box193( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box193 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box193<int> IntBox193;
typedef Box193<double> DoubleBox193;
inline int Total193( const IntBox193 & a, const DoubleBox193 & b, const Record193 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 193 ) + (int)r.Mix( 193u );
}
static const int g_Table193[] = { 193, 386, 580, 967, 1354, 2127 };

struct Record194
{
    int m_Id194;
    double m_Value194;
    char m_Name194[ 28 ];
    Record194 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record194() : m_Id194( 194 ), m_Value194( 194.5 ), m_Next( nullptr ), m_Label( "record194" ), m_Flags7( 582u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name194 ); ++c ) { m_Name194[ c ] = (char)( 65 + ( ( c + 194 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id194 + a * 6 + b - (int)( m_Value194 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 225u ) ^ ( m_Flags7 << 12 ) ^ (unsigned)m_Id194; }
    bool Less( const Record194 & other ) const { return m_Id194 < other.m_Id194 || ( m_Id194 == other.m_Id194 && m_Value194 < other.m_Value194 ); }
};

template <class T> struct Box194
{
    T m_Value;
    T m_Other;
    Box194() : m_Value(), m_Other() {}
    explicit Box194( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box194 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box194<int> IntBox194;
typedef Box194<double> DoubleBox194;
inline int Total194( const IntBox194 & a, const DoubleBox194 & b, const Record194 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 194 ) + (int)r.Mix( 194u );
}
static const int g_Table194[] = { 194, 388, 583, 972, 1361, 2138 };

struct Record195
{
    int m_Id195;
    double m_Value195;
    char m_Name195[ 12 ];
    Record195 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record195() : m_Id195( 195 ), m_Value195( 195.5 ), m_Next( nullptr ), m_Label( "record195" ), m_Flags8( 585u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name195 ); ++c ) { m_Name195[ c ] = (char)( 65 + ( ( c + 195 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id195 + a * 7 + b - (int)( m_Value195 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 226u ) ^ ( m_Flags8 << 0 ) ^ (unsigned)m_Id195; }
    bool Less( const Record195 & other ) const { return m_Id195 < other.m_Id195 || ( m_Id195 == other.m_Id195 && m_Value195 < other.m_Value195 ); }
};

template <class T> struct Box195
{
    T m_Value;
    T m_Other;
    Box195() : m_Value(), m_Other() {}
    explicit Box195( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box195 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box195<int> IntBox195;
typedef Box195<double> DoubleBox195;
inline int Total195( const IntBox195 & a, const DoubleBox195 & b, const Record195 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 195 ) + (int)r.Mix( 195u );
}
static const int g_Table195[] = { 195, 390, 586, 977, 1368, 2149 };

struct Record196
{
    int m_Id196;
    double m_Value196;
    char m_Name196[ 16 ];
    Record196 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record196() : m_Id196( 196 ), m_Value196( 196.5 ), m_Next( nullptr ), m_Label( "record196" ), m_Flags9( 588u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name196 ); ++c ) { m_Name196[ c ] = (char)( 65 + ( ( c + 196 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id196 + a * 1 + b - (int)( m_Value196 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 227u ) ^ ( m_Flags9 << 1 ) ^ (unsigned)m_Id196; }
    bool Less( const Record196 & other ) const { return m_Id196 < other.m_Id196 || ( m_Id196 == other.m_Id196 && m_Value196 < other.m_Value196 ); }
};

template <class T> struct Box196
{
    T m_Value;
    T m_Other;
    Box196() : m_Value(), m_Other() {}
    explicit Box196( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box196 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box196<int> IntBox196;
typedef Box196<double> DoubleBox196;
inline int Total196( const IntBox196 & a, const DoubleBox196 & b, const Record196 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 196 ) + (int)r.Mix( 196u );
}
static const int g_Table196[] = { 196, 392, 589, 982, 1375, 2160 };

struct Record197
{
    int m_Id197;
    double m_Value197;
    char m_Name197[ 20 ];
    Record197 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record197() : m_Id197( 197 ), m_Value197( 197.5 ), m_Next( nullptr ), m_Label( "record197" ), m_Flags10( 591u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name197 ); ++c ) { m_Name197[ c ] = (char)( 65 + ( ( c + 197 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id197 + a * 2 + b - (int)( m_Value197 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 228u ) ^ ( m_Flags10 << 2 ) ^ (unsigned)m_Id197; }
    bool Less( const Record197 & other ) const { return m_Id197 < other.m_Id197 || ( m_Id197 == other.m_Id197 && m_Value197 < other.m_Value197 ); }
};

template <class T> struct Box197
{
    T m_Value;
    T m_Other;
    Box197() : m_Value(), m_Other() {}
    explicit Box197( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box197 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box197<int> IntBox197;
typedef Box197<double> DoubleBox197;
inline int Total197( const IntBox197 & a, const DoubleBox197 & b, const Record197 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 197 ) + (int)r.Mix( 197u );
}
static const int g_Table197[] = { 197, 394, 592, 987, 1382, 2171 };

struct Record198
{
    int m_Id198;
    double m_Value198;
    char m_Name198[ 24 ];
    Record198 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record198() : m_Id198( 198 ), m_Value198( 198.5 ), m_Next( nullptr ), m_Label( "record198" ), m_Flags0( 594u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name198 ); ++c ) { m_Name198[ c ] = (char)( 65 + ( ( c + 198 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id198 + a * 3 + b - (int)( m_Value198 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 229u ) ^ ( m_Flags0 << 3 ) ^ (unsigned)m_Id198; }
    bool Less( const Record198 & other ) const { return m_Id198 < other.m_Id198 || ( m_Id198 == other.m_Id198 && m_Value198 < other.m_Value198 ); }
};

template <class T> struct Box198
{
    T m_Value;
    T m_Other;
    Box198() : m_Value(), m_Other() {}
    explicit Box198( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box198 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box198<int> IntBox198;
typedef Box198<double> DoubleBox198;
inline int Total198( const IntBox198 & a, const DoubleBox198 & b, const Record198 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 198 ) + (int)r.Mix( 198u );
}
static const int g_Table198[] = { 198, 396, 595, 992, 1389, 2182 };

struct Record199
{
    int m_Id199;
    double m_Value199;
    char m_Name199[ 28 ];
    Record199 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record199() : m_Id199( 199 ), m_Value199( 199.5 ), m_Next( nullptr ), m_Label( "record199" ), m_Flags1( 597u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name199 ); ++c ) { m_Name199[ c ] = (char)( 65 + ( ( c + 199 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id199 + a * 4 + b - (int)( m_Value199 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 230u ) ^ ( m_Flags1 << 4 ) ^ (unsigned)m_Id199; }
    bool Less( const Record199 & other ) const { return m_Id199 < other.m_Id199 || ( m_Id199 == other.m_Id199 && m_Value199 < other.m_Value199 ); }
};

template <class T> struct Box199
{
    T m_Value;
    T m_Other;
    Box199() : m_Value(), m_Other() {}
    explicit Box199( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box199 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box199<int> IntBox199;
typedef Box199<double> DoubleBox199;
inline int Total199( const IntBox199 & a, const DoubleBox199 & b, const Record199 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 199 ) + (int)r.Mix( 199u );
}
static const int g_Table199[] = { 199, 398, 598, 997, 1396, 2193 };

struct Record200
{
    int m_Id200;
    double m_Value200;
    char m_Name200[ 12 ];
    Record200 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record200() : m_Id200( 200 ), m_Value200( 200.5 ), m_Next( nullptr ), m_Label( "record200" ), m_Flags2( 600u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name200 ); ++c ) { m_Name200[ c ] = (char)( 65 + ( ( c + 200 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id200 + a * 5 + b - (int)( m_Value200 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 231u ) ^ ( m_Flags2 << 5 ) ^ (unsigned)m_Id200; }
    bool Less( const Record200 & other ) const { return m_Id200 < other.m_Id200 || ( m_Id200 == other.m_Id200 && m_Value200 < other.m_Value200 ); }
};

template <class T> struct Box200
{
    T m_Value;
    T m_Other;
    Box200() : m_Value(), m_Other() {}
    explicit Box200( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box200 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box200<int> IntBox200;
typedef Box200<double> DoubleBox200;
inline int Total200( const IntBox200 & a, const DoubleBox200 & b, const Record200 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 200 ) + (int)r.Mix( 200u );
}
static const int g_Table200[] = { 200, 400, 601, 1002, 1403, 2204 };

struct Record201
{
    int m_Id201;
    double m_Value201;
    char m_Name201[ 16 ];
    Record201 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record201() : m_Id201( 201 ), m_Value201( 201.5 ), m_Next( nullptr ), m_Label( "record201" ), m_Flags3( 603u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name201 ); ++c ) { m_Name201[ c ] = (char)( 65 + ( ( c + 201 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id201 + a * 6 + b - (int)( m_Value201 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 232u ) ^ ( m_Flags3 << 6 ) ^ (unsigned)m_Id201; }
    bool Less( const Record201 & other ) const { return m_Id201 < other.m_Id201 || ( m_Id201 == other.m_Id201 && m_Value201 < other.m_Value201 ); }
};

template <class T> struct Box201
{
    T m_Value;
    T m_Other;
    Box201() : m_Value(), m_Other() {}
    explicit Box201( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box201 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box201<int> IntBox201;
typedef Box201<double> DoubleBox201;
inline int Total201( const IntBox201 & a, const DoubleBox201 & b, const Record201 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 201 ) + (int)r.Mix( 201u );
}
static const int g_Table201[] = { 201, 402, 604, 1007, 1410, 2215 };

struct Record202
{
    int m_Id202;
    double m_Value202;
    char m_Name202[ 20 ];
    Record202 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record202() : m_Id202( 202 ), m_Value202( 202.5 ), m_Next( nullptr ), m_Label( "record202" ), m_Flags4( 606u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name202 ); ++c ) { m_Name202[ c ] = (char)( 65 + ( ( c + 202 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id202 + a * 7 + b - (int)( m_Value202 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 233u ) ^ ( m_Flags4 << 7 ) ^ (unsigned)m_Id202; }
    bool Less( const Record202 & other ) const { return m_Id202 < other.m_Id202 || ( m_Id202 == other.m_Id202 && m_Value202 < other.m_Value202 ); }
};

template <class T> struct Box202
{
    T m_Value;
    T m_Other;
    Box202() : m_Value(), m_Other() {}
    explicit Box202( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box202 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box202<int> IntBox202;
typedef Box202<double> DoubleBox202;
inline int Total202( const IntBox202 & a, const DoubleBox202 & b, const Record202 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 202 ) + (int)r.Mix( 202u );
}
static const int g_Table202[] = { 202, 404, 607, 1012, 1417, 2226 };

struct Record203
{
    int m_Id203;
    double m_Value203;
    char m_Name203[ 24 ];
    Record203 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record203() : m_Id203( 203 ), m_Value203( 203.5 ), m_Next( nullptr ), m_Label( "record203" ), m_Flags5( 609u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name203 ); ++c ) { m_Name203[ c ] = (char)( 65 + ( ( c + 203 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id203 + a * 1 + b - (int)( m_Value203 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 234u ) ^ ( m_Flags5 << 8 ) ^ (unsigned)m_Id203; }
    bool Less( const Record203 & other ) const { return m_Id203 < other.m_Id203 || ( m_Id203 == other.m_Id203 && m_Value203 < other.m_Value203 ); }
};

template <class T> struct Box203
{
    T m_Value;
    T m_Other;
    Box203() : m_Value(), m_Other() {}
    explicit Box203( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box203 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box203<int> IntBox203;
typedef Box203<double> DoubleBox203;
inline int Total203( const IntBox203 & a, const DoubleBox203 & b, const Record203 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 203 ) + (int)r.Mix( 203u );
}
static const int g_Table203[] = { 203, 406, 610, 1017, 1424, 2237 };

struct Record204
{
    int m_Id204;
    double m_Value204;
    char m_Name204[ 28 ];
    Record204 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record204() : m_Id204( 204 ), m_Value204( 204.5 ), m_Next( nullptr ), m_Label( "record204" ), m_Flags6( 612u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name204 ); ++c ) { m_Name204[ c ] = (char)( 65 + ( ( c + 204 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id204 + a * 2 + b - (int)( m_Value204 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 235u ) ^ ( m_Flags6 << 9 ) ^ (unsigned)m_Id204; }
    bool Less( const Record204 & other ) const { return m_Id204 < other.m_Id204 || ( m_Id204 == other.m_Id204 && m_Value204 < other.m_Value204 ); }
};

template <class T> struct Box204
{
    T m_Value;
    T m_Other;
    Box204() : m_Value(), m_Other() {}
    explicit Box204( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box204 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box204<int> IntBox204;
typedef Box204<double> DoubleBox204;
inline int Total204( const IntBox204 & a, const DoubleBox204 & b, const Record204 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 204 ) + (int)r.Mix( 204u );
}
static const int g_Table204[] = { 204, 408, 613, 1022, 1431, 2248 };

struct Record205
{
    int m_Id205;
    double m_Value205;
    char m_Name205[ 12 ];
    Record205 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record205() : m_Id205( 205 ), m_Value205( 205.5 ), m_Next( nullptr ), m_Label( "record205" ), m_Flags7( 615u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name205 ); ++c ) { m_Name205[ c ] = (char)( 65 + ( ( c + 205 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id205 + a * 3 + b - (int)( m_Value205 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 236u ) ^ ( m_Flags7 << 10 ) ^ (unsigned)m_Id205; }
    bool Less( const Record205 & other ) const { return m_Id205 < other.m_Id205 || ( m_Id205 == other.m_Id205 && m_Value205 < other.m_Value205 ); }
};

template <class T> struct Box205
{
    T m_Value;
    T m_Other;
    Box205() : m_Value(), m_Other() {}
    explicit Box205( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box205 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box205<int> IntBox205;
typedef Box205<double> DoubleBox205;
inline int Total205( const IntBox205 & a, const DoubleBox205 & b, const Record205 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 205 ) + (int)r.Mix( 205u );
}
static const int g_Table205[] = { 205, 410, 616, 1027, 1438, 2259 };

struct Record206
{
    int m_Id206;
    double m_Value206;
    char m_Name206[ 16 ];
    Record206 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record206() : m_Id206( 206 ), m_Value206( 206.5 ), m_Next( nullptr ), m_Label( "record206" ), m_Flags8( 618u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name206 ); ++c ) { m_Name206[ c ] = (char)( 65 + ( ( c + 206 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id206 + a * 4 + b - (int)( m_Value206 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 237u ) ^ ( m_Flags8 << 11 ) ^ (unsigned)m_Id206; }
    bool Less( const Record206 & other ) const { return m_Id206 < other.m_Id206 || ( m_Id206 == other.m_Id206 && m_Value206 < other.m_Value206 ); }
};

template <class T> struct Box206
{
    T m_Value;
    T m_Other;
    Box206() : m_Value(), m_Other() {}
    explicit Box206( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box206 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box206<int> IntBox206;
typedef Box206<double> DoubleBox206;
inline int Total206( const IntBox206 & a, const DoubleBox206 & b, const Record206 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 206 ) + (int)r.Mix( 206u );
}
static const int g_Table206[] = { 206, 412, 619, 1032, 1445, 2270 };

struct Record207
{
    int m_Id207;
    double m_Value207;
    char m_Name207[ 20 ];
    Record207 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record207() : m_Id207( 207 ), m_Value207( 207.5 ), m_Next( nullptr ), m_Label( "record207" ), m_Flags9( 621u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name207 ); ++c ) { m_Name207[ c ] = (char)( 65 + ( ( c + 207 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id207 + a * 5 + b - (int)( m_Value207 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 238u ) ^ ( m_Flags9 << 12 ) ^ (unsigned)m_Id207; }
    bool Less( const Record207 & other ) const { return m_Id207 < other.m_Id207 || ( m_Id207 == other.m_Id207 && m_Value207 < other.m_Value207 ); }
};

template <class T> struct Box207
{
    T m_Value;
    T m_Other;
    Box207() : m_Value(), m_Other() {}
    explicit Box207( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box207 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box207<int> IntBox207;
typedef Box207<double> DoubleBox207;
inline int Total207( const IntBox207 & a, const DoubleBox207 & b, const Record207 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 207 ) + (int)r.Mix( 207u );
}
static const int g_Table207[] = { 207, 414, 622, 1037, 1452, 2281 };

struct Record208
{
    int m_Id208;
    double m_Value208;
    char m_Name208[ 24 ];
    Record208 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record208() : m_Id208( 208 ), m_Value208( 208.5 ), m_Next( nullptr ), m_Label( "record208" ), m_Flags10( 624u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name208 ); ++c ) { m_Name208[ c ] = (char)( 65 + ( ( c + 208 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id208 + a * 6 + b - (int)( m_Value208 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 239u ) ^ ( m_Flags10 << 0 ) ^ (unsigned)m_Id208; }
    bool Less( const Record208 & other ) const { return m_Id208 < other.m_Id208 || ( m_Id208 == other.m_Id208 && m_Value208 < other.m_Value208 ); }
};

template <class T> struct Box208
{
    T m_Value;
    T m_Other;
    Box208() : m_Value(), m_Other() {}
    explicit Box208( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box208 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box208<int> IntBox208;
typedef Box208<double> DoubleBox208;
inline int Total208( const IntBox208 & a, const DoubleBox208 & b, const Record208 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 208 ) + (int)r.Mix( 208u );
}
static const int g_Table208[] = { 208, 416, 625, 1042, 1459, 2292 };

struct Record209
{
    int m_Id209;
    double m_Value209;
    char m_Name209[ 28 ];
    Record209 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record209() : m_Id209( 209 ), m_Value209( 209.5 ), m_Next( nullptr ), m_Label( "record209" ), m_Flags0( 627u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name209 ); ++c ) { m_Name209[ c ] = (char)( 65 + ( ( c + 209 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id209 + a * 7 + b - (int)( m_Value209 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 240u ) ^ ( m_Flags0 << 1 ) ^ (unsigned)m_Id209; }
    bool Less( const Record209 & other ) const { return m_Id209 < other.m_Id209 || ( m_Id209 == other.m_Id209 && m_Value209 < other.m_Value209 ); }
};

template <class T> struct Box209
{
    T m_Value;
    T m_Other;
    Box209() : m_Value(), m_Other() {}
    explicit Box209( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box209 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box209<int> IntBox209;
typedef Box209<double> DoubleBox209;
inline int Total209( const IntBox209 & a, const DoubleBox209 & b, const Record209 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 209 ) + (int)r.Mix( 209u );
}
static const int g_Table209[] = { 209, 418, 628, 1047, 1466, 2303 };

struct Record210
{
    int m_Id210;
    double m_Value210;
    char m_Name210[ 12 ];
    Record210 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record210() : m_Id210( 210 ), m_Value210( 210.5 ), m_Next( nullptr ), m_Label( "record210" ), m_Flags1( 630u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name210 ); ++c ) { m_Name210[ c ] = (char)( 65 + ( ( c + 210 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id210 + a * 1 + b - (int)( m_Value210 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 241u ) ^ ( m_Flags1 << 2 ) ^ (unsigned)m_Id210; }
    bool Less( const Record210 & other ) const { return m_Id210 < other.m_Id210 || ( m_Id210 == other.m_Id210 && m_Value210 < other.m_Value210 ); }
};

template <class T> struct Box210
{
    T m_Value;
    T m_Other;
    Box210() : m_Value(), m_Other() {}
    explicit Box210( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box210 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box210<int> IntBox210;
typedef Box210<double> DoubleBox210;
inline int Total210( const IntBox210 & a, const DoubleBox210 & b, const Record210 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 210 ) + (int)r.Mix( 210u );
}
static const int g_Table210[] = { 210, 420, 631, 1052, 1473, 2314 };

struct Record211
{
    int m_Id211;
    double m_Value211;
    char m_Name211[ 16 ];
    Record211 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record211() : m_Id211( 211 ), m_Value211( 211.5 ), m_Next( nullptr ), m_Label( "record211" ), m_Flags2( 633u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name211 ); ++c ) { m_Name211[ c ] = (char)( 65 + ( ( c + 211 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id211 + a * 2 + b - (int)( m_Value211 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 242u ) ^ ( m_Flags2 << 3 ) ^ (unsigned)m_Id211; }
    bool Less( const Record211 & other ) const { return m_Id211 < other.m_Id211 || ( m_Id211 == other.m_Id211 && m_Value211 < other.m_Value211 ); }
};

template <class T> struct Box211
{
    T m_Value;
    T m_Other;
    Box211() : m_Value(), m_Other() {}
    explicit Box211( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box211 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box211<int> IntBox211;
typedef Box211<double> DoubleBox211;
inline int Total211( const IntBox211 & a, const DoubleBox211 & b, const Record211 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 211 ) + (int)r.Mix( 211u );
}
static const int g_Table211[] = { 211, 422, 634, 1057, 1480, 2325 };

struct Record212
{
    int m_Id212;
    double m_Value212;
    char m_Name212[ 20 ];
    Record212 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record212() : m_Id212( 212 ), m_Value212( 212.5 ), m_Next( nullptr ), m_Label( "record212" ), m_Flags3( 636u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name212 ); ++c ) { m_Name212[ c ] = (char)( 65 + ( ( c + 212 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id212 + a * 3 + b - (int)( m_Value212 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 243u ) ^ ( m_Flags3 << 4 ) ^ (unsigned)m_Id212; }
    bool Less( const Record212 & other ) const { return m_Id212 < other.m_Id212 || ( m_Id212 == other.m_Id212 && m_Value212 < other.m_Value212 ); }
};

template <class T> struct Box212
{
    T m_Value;
    T m_Other;
    Box212() : m_Value(), m_Other() {}
    explicit Box212( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box212 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box212<int> IntBox212;
typedef Box212<double> DoubleBox212;
inline int Total212( const IntBox212 & a, const DoubleBox212 & b, const Record212 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 212 ) + (int)r.Mix( 212u );
}
static const int g_Table212[] = { 212, 424, 637, 1062, 1487, 2336 };

struct Record213
{
    int m_Id213;
    double m_Value213;
    char m_Name213[ 24 ];
    Record213 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record213() : m_Id213( 213 ), m_Value213( 213.5 ), m_Next( nullptr ), m_Label( "record213" ), m_Flags4( 639u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name213 ); ++c ) { m_Name213[ c ] = (char)( 65 + ( ( c + 213 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id213 + a * 4 + b - (int)( m_Value213 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 244u ) ^ ( m_Flags4 << 5 ) ^ (unsigned)m_Id213; }
    bool Less( const Record213 & other ) const { return m_Id213 < other.m_Id213 || ( m_Id213 == other.m_Id213 && m_Value213 < other.m_Value213 ); }
};

template <class T> struct Box213
{
    T m_Value;
    T m_Other;
    Box213() : m_Value(), m_Other() {}
    explicit Box213( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box213 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box213<int> IntBox213;
typedef Box213<double> DoubleBox213;
inline int Total213( const IntBox213 & a, const DoubleBox213 & b, const Record213 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 213 ) + (int)r.Mix( 213u );
}
static const int g_Table213[] = { 213, 426, 640, 1067, 1494, 2347 };

struct Record214
{
    int m_Id214;
    double m_Value214;
    char m_Name214[ 28 ];
    Record214 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record214() : m_Id214( 214 ), m_Value214( 214.5 ), m_Next( nullptr ), m_Label( "record214" ), m_Flags5( 642u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name214 ); ++c ) { m_Name214[ c ] = (char)( 65 + ( ( c + 214 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id214 + a * 5 + b - (int)( m_Value214 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 245u ) ^ ( m_Flags5 << 6 ) ^ (unsigned)m_Id214; }
    bool Less( const Record214 & other ) const { return m_Id214 < other.m_Id214 || ( m_Id214 == other.m_Id214 && m_Value214 < other.m_Value214 ); }
};

template <class T> struct Box214
{
    T m_Value;
    T m_Other;
    Box214() : m_Value(), m_Other() {}
    explicit Box214( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box214 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box214<int> IntBox214;
typedef Box214<double> DoubleBox214;
inline int Total214( const IntBox214 & a, const DoubleBox214 & b, const Record214 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 214 ) + (int)r.Mix( 214u );
}
static const int g_Table214[] = { 214, 428, 643, 1072, 1501, 2358 };

struct Record215
{
    int m_Id215;
    double m_Value215;
    char m_Name215[ 12 ];
    Record215 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record215() : m_Id215( 215 ), m_Value215( 215.5 ), m_Next( nullptr ), m_Label( "record215" ), m_Flags6( 645u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name215 ); ++c ) { m_Name215[ c ] = (char)( 65 + ( ( c + 215 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id215 + a * 6 + b - (int)( m_Value215 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 246u ) ^ ( m_Flags6 << 7 ) ^ (unsigned)m_Id215; }
    bool Less( const Record215 & other ) const { return m_Id215 < other.m_Id215 || ( m_Id215 == other.m_Id215 && m_Value215 < other.m_Value215 ); }
};

template <class T> struct Box215
{
    T m_Value;
    T m_Other;
    Box215() : m_Value(), m_Other() {}
    explicit Box215( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box215 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box215<int> IntBox215;
typedef Box215<double> DoubleBox215;
inline int Total215( const IntBox215 & a, const DoubleBox215 & b, const Record215 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 215 ) + (int)r.Mix( 215u );
}
static const int g_Table215[] = { 215, 430, 646, 1077, 1508, 2369 };

struct Record216
{
    int m_Id216;
    double m_Value216;
    char m_Name216[ 16 ];
    Record216 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record216() : m_Id216( 216 ), m_Value216( 216.5 ), m_Next( nullptr ), m_Label( "record216" ), m_Flags7( 648u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name216 ); ++c ) { m_Name216[ c ] = (char)( 65 + ( ( c + 216 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id216 + a * 7 + b - (int)( m_Value216 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 247u ) ^ ( m_Flags7 << 8 ) ^ (unsigned)m_Id216; }
    bool Less( const Record216 & other ) const { return m_Id216 < other.m_Id216 || ( m_Id216 == other.m_Id216 && m_Value216 < other.m_Value216 ); }
};

template <class T> struct Box216
{
    T m_Value;
    T m_Other;
    Box216() : m_Value(), m_Other() {}
    explicit Box216( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box216 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box216<int> IntBox216;
typedef Box216<double> DoubleBox216;
inline int Total216( const IntBox216 & a, const DoubleBox216 & b, const Record216 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 216 ) + (int)r.Mix( 216u );
}
static const int g_Table216[] = { 216, 432, 649, 1082, 1515, 2380 };

struct Record217
{
    int m_Id217;
    double m_Value217;
    char m_Name217[ 20 ];
    Record217 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record217() : m_Id217( 217 ), m_Value217( 217.5 ), m_Next( nullptr ), m_Label( "record217" ), m_Flags8( 651u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name217 ); ++c ) { m_Name217[ c ] = (char)( 65 + ( ( c + 217 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id217 + a * 1 + b - (int)( m_Value217 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 248u ) ^ ( m_Flags8 << 9 ) ^ (unsigned)m_Id217; }
    bool Less( const Record217 & other ) const { return m_Id217 < other.m_Id217 || ( m_Id217 == other.m_Id217 && m_Value217 < other.m_Value217 ); }
};

template <class T> struct Box217
{
    T m_Value;
    T m_Other;
    Box217() : m_Value(), m_Other() {}
    explicit Box217( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box217 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box217<int> IntBox217;
typedef Box217<double> DoubleBox217;
inline int Total217( const IntBox217 & a, const DoubleBox217 & b, const Record217 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 217 ) + (int)r.Mix( 217u );
}
static const int g_Table217[] = { 217, 434, 652, 1087, 1522, 2391 };

struct Record218
{
    int m_Id218;
    double m_Value218;
    char m_Name218[ 24 ];
    Record218 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record218() : m_Id218( 218 ), m_Value218( 218.5 ), m_Next( nullptr ), m_Label( "record218" ), m_Flags9( 654u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name218 ); ++c ) { m_Name218[ c ] = (char)( 65 + ( ( c + 218 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id218 + a * 2 + b - (int)( m_Value218 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 249u ) ^ ( m_Flags9 << 10 ) ^ (unsigned)m_Id218; }
    bool Less( const Record218 & other ) const { return m_Id218 < other.m_Id218 || ( m_Id218 == other.m_Id218 && m_Value218 < other.m_Value218 ); }
};

template <class T> struct Box218
{
    T m_Value;
    T m_Other;
    Box218() : m_Value(), m_Other() {}
    explicit Box218( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box218 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box218<int> IntBox218;
typedef Box218<double> DoubleBox218;
inline int Total218( const IntBox218 & a, const DoubleBox218 & b, const Record218 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 218 ) + (int)r.Mix( 218u );
}
static const int g_Table218[] = { 218, 436, 655, 1092, 1529, 2402 };

struct Record219
{
    int m_Id219;
    double m_Value219;
    char m_Name219[ 28 ];
    Record219 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record219() : m_Id219( 219 ), m_Value219( 219.5 ), m_Next( nullptr ), m_Label( "record219" ), m_Flags10( 657u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name219 ); ++c ) { m_Name219[ c ] = (char)( 65 + ( ( c + 219 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id219 + a * 3 + b - (int)( m_Value219 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 250u ) ^ ( m_Flags10 << 11 ) ^ (unsigned)m_Id219; }
    bool Less( const Record219 & other ) const { return m_Id219 < other.m_Id219 || ( m_Id219 == other.m_Id219 && m_Value219 < other.m_Value219 ); }
};

template <class T> struct Box219
{
    T m_Value;
    T m_Other;
    Box219() : m_Value(), m_Other() {}
    explicit Box219( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box219 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box219<int> IntBox219;
typedef Box219<double> DoubleBox219;
inline int Total219( const IntBox219 & a, const DoubleBox219 & b, const Record219 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 219 ) + (int)r.Mix( 219u );
}
static const int g_Table219[] = { 219, 438, 658, 1097, 1536, 2413 };

struct Record220
{
    int m_Id220;
    double m_Value220;
    char m_Name220[ 12 ];
    Record220 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record220() : m_Id220( 220 ), m_Value220( 220.5 ), m_Next( nullptr ), m_Label( "record220" ), m_Flags0( 660u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name220 ); ++c ) { m_Name220[ c ] = (char)( 65 + ( ( c + 220 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id220 + a * 4 + b - (int)( m_Value220 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 251u ) ^ ( m_Flags0 << 12 ) ^ (unsigned)m_Id220; }
    bool Less( const Record220 & other ) const { return m_Id220 < other.m_Id220 || ( m_Id220 == other.m_Id220 && m_Value220 < other.m_Value220 ); }
};

template <class T> struct Box220
{
    T m_Value;
    T m_Other;
    Box220() : m_Value(), m_Other() {}
    explicit Box220( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box220 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box220<int> IntBox220;
typedef Box220<double> DoubleBox220;
inline int Total220( const IntBox220 & a, const DoubleBox220 & b, const Record220 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 220 ) + (int)r.Mix( 220u );
}
static const int g_Table220[] = { 220, 440, 661, 1102, 1543, 2424 };

struct Record221
{
    int m_Id221;
    double m_Value221;
    char m_Name221[ 16 ];
    Record221 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record221() : m_Id221( 221 ), m_Value221( 221.5 ), m_Next( nullptr ), m_Label( "record221" ), m_Flags1( 663u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name221 ); ++c ) { m_Name221[ c ] = (char)( 65 + ( ( c + 221 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id221 + a * 5 + b - (int)( m_Value221 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 252u ) ^ ( m_Flags1 << 0 ) ^ (unsigned)m_Id221; }
    bool Less( const Record221 & other ) const { return m_Id221 < other.m_Id221 || ( m_Id221 == other.m_Id221 && m_Value221 < other.m_Value221 ); }
};

template <class T> struct Box221
{
    T m_Value;
    T m_Other;
    Box221() : m_Value(), m_Other() {}
    explicit Box221( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box221 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box221<int> IntBox221;
typedef Box221<double> DoubleBox221;
inline int Total221( const IntBox221 & a, const DoubleBox221 & b, const Record221 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 221 ) + (int)r.Mix( 221u );
}
static const int g_Table221[] = { 221, 442, 664, 1107, 1550, 2435 };

struct Record222
{
    int m_Id222;
    double m_Value222;
    char m_Name222[ 20 ];
    Record222 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record222() : m_Id222( 222 ), m_Value222( 222.5 ), m_Next( nullptr ), m_Label( "record222" ), m_Flags2( 666u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name222 ); ++c ) { m_Name222[ c ] = (char)( 65 + ( ( c + 222 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id222 + a * 6 + b - (int)( m_Value222 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 253u ) ^ ( m_Flags2 << 1 ) ^ (unsigned)m_Id222; }
    bool Less( const Record222 & other ) const { return m_Id222 < other.m_Id222 || ( m_Id222 == other.m_Id222 && m_Value222 < other.m_Value222 ); }
};

template <class T> struct Box222
{
    T m_Value;
    T m_Other;
    Box222() : m_Value(), m_Other() {}
    explicit Box222( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box222 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box222<int> IntBox222;
typedef Box222<double> DoubleBox222;
inline int Total222( const IntBox222 & a, const DoubleBox222 & b, const Record222 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 222 ) + (int)r.Mix( 222u );
}
static const int g_Table222[] = { 222, 444, 667, 1112, 1557, 2446 };

struct Record223
{
    int m_Id223;
    double m_Value223;
    char m_Name223[ 24 ];
    Record223 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record223() : m_Id223( 223 ), m_Value223( 223.5 ), m_Next( nullptr ), m_Label( "record223" ), m_Flags3( 669u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name223 ); ++c ) { m_Name223[ c ] = (char)( 65 + ( ( c + 223 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id223 + a * 7 + b - (int)( m_Value223 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 254u ) ^ ( m_Flags3 << 2 ) ^ (unsigned)m_Id223; }
    bool Less( const Record223 & other ) const { return m_Id223 < other.m_Id223 || ( m_Id223 == other.m_Id223 && m_Value223 < other.m_Value223 ); }
};

template <class T> struct Box223
{
    T m_Value;
    T m_Other;
    Box223() : m_Value(), m_Other() {}
    explicit Box223( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box223 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box223<int> IntBox223;
typedef Box223<double> DoubleBox223;
inline int Total223( const IntBox223 & a, const DoubleBox223 & b, const Record223 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 223 ) + (int)r.Mix( 223u );
}
static const int g_Table223[] = { 223, 446, 670, 1117, 1564, 2457 };

struct Record224
{
    int m_Id224;
    double m_Value224;
    char m_Name224[ 28 ];
    Record224 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record224() : m_Id224( 224 ), m_Value224( 224.5 ), m_Next( nullptr ), m_Label( "record224" ), m_Flags4( 672u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name224 ); ++c ) { m_Name224[ c ] = (char)( 65 + ( ( c + 224 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id224 + a * 1 + b - (int)( m_Value224 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 255u ) ^ ( m_Flags4 << 3 ) ^ (unsigned)m_Id224; }
    bool Less( const Record224 & other ) const { return m_Id224 < other.m_Id224 || ( m_Id224 == other.m_Id224 && m_Value224 < other.m_Value224 ); }
};

template <class T> struct Box224
{
    T m_Value;
    T m_Other;
    Box224() : m_Value(), m_Other() {}
    explicit Box224( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box224 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box224<int> IntBox224;
typedef Box224<double> DoubleBox224;
inline int Total224( const IntBox224 & a, const DoubleBox224 & b, const Record224 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 224 ) + (int)r.Mix( 224u );
}
static const int g_Table224[] = { 224, 448, 673, 1122, 1571, 2468 };

struct Record225
{
    int m_Id225;
    double m_Value225;
    char m_Name225[ 12 ];
    Record225 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record225() : m_Id225( 225 ), m_Value225( 225.5 ), m_Next( nullptr ), m_Label( "record225" ), m_Flags5( 675u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name225 ); ++c ) { m_Name225[ c ] = (char)( 65 + ( ( c + 225 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id225 + a * 2 + b - (int)( m_Value225 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 256u ) ^ ( m_Flags5 << 4 ) ^ (unsigned)m_Id225; }
    bool Less( const Record225 & other ) const { return m_Id225 < other.m_Id225 || ( m_Id225 == other.m_Id225 && m_Value225 < other.m_Value225 ); }
};

template <class T> struct Box225
{
    T m_Value;
    T m_Other;
    Box225() : m_Value(), m_Other() {}
    explicit Box225( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box225 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box225<int> IntBox225;
typedef Box225<double> DoubleBox225;
inline int Total225( const IntBox225 & a, const DoubleBox225 & b, const Record225 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 225 ) + (int)r.Mix( 225u );
}
static const int g_Table225[] = { 225, 450, 676, 1127, 1578, 2479 };

struct Record226
{
    int m_Id226;
    double m_Value226;
    char m_Name226[ 16 ];
    Record226 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record226() : m_Id226( 226 ), m_Value226( 226.5 ), m_Next( nullptr ), m_Label( "record226" ), m_Flags6( 678u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name226 ); ++c ) { m_Name226[ c ] = (char)( 65 + ( ( c + 226 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id226 + a * 3 + b - (int)( m_Value226 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 257u ) ^ ( m_Flags6 << 5 ) ^ (unsigned)m_Id226; }
    bool Less( const Record226 & other ) const { return m_Id226 < other.m_Id226 || ( m_Id226 == other.m_Id226 && m_Value226 < other.m_Value226 ); }
};

template <class T> struct Box226
{
    T m_Value;
    T m_Other;
    Box226() : m_Value(), m_Other() {}
    explicit Box226( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box226 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box226<int> IntBox226;
typedef Box226<double> DoubleBox226;
inline int Total226( const IntBox226 & a, const DoubleBox226 & b, const Record226 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 226 ) + (int)r.Mix( 226u );
}
static const int g_Table226[] = { 226, 452, 679, 1132, 1585, 2490 };

struct Record227
{
    int m_Id227;
    double m_Value227;
    char m_Name227[ 20 ];
    Record227 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record227() : m_Id227( 227 ), m_Value227( 227.5 ), m_Next( nullptr ), m_Label( "record227" ), m_Flags7( 681u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name227 ); ++c ) { m_Name227[ c ] = (char)( 65 + ( ( c + 227 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id227 + a * 4 + b - (int)( m_Value227 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 258u ) ^ ( m_Flags7 << 6 ) ^ (unsigned)m_Id227; }
    bool Less( const Record227 & other ) const { return m_Id227 < other.m_Id227 || ( m_Id227 == other.m_Id227 && m_Value227 < other.m_Value227 ); }
};

template <class T> struct Box227
{
    T m_Value;
    T m_Other;
    Box227() : m_Value(), m_Other() {}
    explicit Box227( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box227 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box227<int> IntBox227;
typedef Box227<double> DoubleBox227;
inline int Total227( const IntBox227 & a, const DoubleBox227 & b, const Record227 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 227 ) + (int)r.Mix( 227u );
}
static const int g_Table227[] = { 227, 454, 682, 1137, 1592, 2501 };

struct Record228
{
    int m_Id228;
    double m_Value228;
    char m_Name228[ 24 ];
    Record228 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record228() : m_Id228( 228 ), m_Value228( 228.5 ), m_Next( nullptr ), m_Label( "record228" ), m_Flags8( 684u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name228 ); ++c ) { m_Name228[ c ] = (char)( 65 + ( ( c + 228 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id228 + a * 5 + b - (int)( m_Value228 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 259u ) ^ ( m_Flags8 << 7 ) ^ (unsigned)m_Id228; }
    bool Less( const Record228 & other ) const { return m_Id228 < other.m_Id228 || ( m_Id228 == other.m_Id228 && m_Value228 < other.m_Value228 ); }
};

template <class T> struct Box228
{
    T m_Value;
    T m_Other;
    Box228() : m_Value(), m_Other() {}
    explicit Box228( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box228 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box228<int> IntBox228;
typedef Box228<double> DoubleBox228;
inline int Total228( const IntBox228 & a, const DoubleBox228 & b, const Record228 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 228 ) + (int)r.Mix( 228u );
}
static const int g_Table228[] = { 228, 456, 685, 1142, 1599, 2512 };

struct Record229
{
    int m_Id229;
    double m_Value229;
    char m_Name229[ 28 ];
    Record229 * m_Next;
    const char * m_Label;
    unsigned m_Flags9;
    Record229() : m_Id229( 229 ), m_Value229( 229.5 ), m_Next( nullptr ), m_Label( "record229" ), m_Flags9( 687u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name229 ); ++c ) { m_Name229[ c ] = (char)( 65 + ( ( c + 229 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id229 + a * 6 + b - (int)( m_Value229 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 260u ) ^ ( m_Flags9 << 8 ) ^ (unsigned)m_Id229; }
    bool Less( const Record229 & other ) const { return m_Id229 < other.m_Id229 || ( m_Id229 == other.m_Id229 && m_Value229 < other.m_Value229 ); }
};

template <class T> struct Box229
{
    T m_Value;
    T m_Other;
    Box229() : m_Value(), m_Other() {}
    explicit Box229( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box229 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box229<int> IntBox229;
typedef Box229<double> DoubleBox229;
inline int Total229( const IntBox229 & a, const DoubleBox229 & b, const Record229 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 229 ) + (int)r.Mix( 229u );
}
static const int g_Table229[] = { 229, 458, 688, 1147, 1606, 2523 };

struct Record230
{
    int m_Id230;
    double m_Value230;
    char m_Name230[ 12 ];
    Record230 * m_Next;
    const char * m_Label;
    unsigned m_Flags10;
    Record230() : m_Id230( 230 ), m_Value230( 230.5 ), m_Next( nullptr ), m_Label( "record230" ), m_Flags10( 690u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name230 ); ++c ) { m_Name230[ c ] = (char)( 65 + ( ( c + 230 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id230 + a * 7 + b - (int)( m_Value230 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 261u ) ^ ( m_Flags10 << 9 ) ^ (unsigned)m_Id230; }
    bool Less( const Record230 & other ) const { return m_Id230 < other.m_Id230 || ( m_Id230 == other.m_Id230 && m_Value230 < other.m_Value230 ); }
};

template <class T> struct Box230
{
    T m_Value;
    T m_Other;
    Box230() : m_Value(), m_Other() {}
    explicit Box230( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box230 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box230<int> IntBox230;
typedef Box230<double> DoubleBox230;
inline int Total230( const IntBox230 & a, const DoubleBox230 & b, const Record230 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 230 ) + (int)r.Mix( 230u );
}
static const int g_Table230[] = { 230, 460, 691, 1152, 1613, 2534 };

struct Record231
{
    int m_Id231;
    double m_Value231;
    char m_Name231[ 16 ];
    Record231 * m_Next;
    const char * m_Label;
    unsigned m_Flags0;
    Record231() : m_Id231( 231 ), m_Value231( 231.5 ), m_Next( nullptr ), m_Label( "record231" ), m_Flags0( 693u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name231 ); ++c ) { m_Name231[ c ] = (char)( 65 + ( ( c + 231 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id231 + a * 1 + b - (int)( m_Value231 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 262u ) ^ ( m_Flags0 << 10 ) ^ (unsigned)m_Id231; }
    bool Less( const Record231 & other ) const { return m_Id231 < other.m_Id231 || ( m_Id231 == other.m_Id231 && m_Value231 < other.m_Value231 ); }
};

template <class T> struct Box231
{
    T m_Value;
    T m_Other;
    Box231() : m_Value(), m_Other() {}
    explicit Box231( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box231 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box231<int> IntBox231;
typedef Box231<double> DoubleBox231;
inline int Total231( const IntBox231 & a, const DoubleBox231 & b, const Record231 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 231 ) + (int)r.Mix( 231u );
}
static const int g_Table231[] = { 231, 462, 694, 1157, 1620, 2545 };

struct Record232
{
    int m_Id232;
    double m_Value232;
    char m_Name232[ 20 ];
    Record232 * m_Next;
    const char * m_Label;
    unsigned m_Flags1;
    Record232() : m_Id232( 232 ), m_Value232( 232.5 ), m_Next( nullptr ), m_Label( "record232" ), m_Flags1( 696u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name232 ); ++c ) { m_Name232[ c ] = (char)( 65 + ( ( c + 232 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id232 + a * 2 + b - (int)( m_Value232 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 263u ) ^ ( m_Flags1 << 11 ) ^ (unsigned)m_Id232; }
    bool Less( const Record232 & other ) const { return m_Id232 < other.m_Id232 || ( m_Id232 == other.m_Id232 && m_Value232 < other.m_Value232 ); }
};

template <class T> struct Box232
{
    T m_Value;
    T m_Other;
    Box232() : m_Value(), m_Other() {}
    explicit Box232( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box232 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box232<int> IntBox232;
typedef Box232<double> DoubleBox232;
inline int Total232( const IntBox232 & a, const DoubleBox232 & b, const Record232 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 232 ) + (int)r.Mix( 232u );
}
static const int g_Table232[] = { 232, 464, 697, 1162, 1627, 2556 };

struct Record233
{
    int m_Id233;
    double m_Value233;
    char m_Name233[ 24 ];
    Record233 * m_Next;
    const char * m_Label;
    unsigned m_Flags2;
    Record233() : m_Id233( 233 ), m_Value233( 233.5 ), m_Next( nullptr ), m_Label( "record233" ), m_Flags2( 699u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name233 ); ++c ) { m_Name233[ c ] = (char)( 65 + ( ( c + 233 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id233 + a * 3 + b - (int)( m_Value233 * 4 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 264u ) ^ ( m_Flags2 << 12 ) ^ (unsigned)m_Id233; }
    bool Less( const Record233 & other ) const { return m_Id233 < other.m_Id233 || ( m_Id233 == other.m_Id233 && m_Value233 < other.m_Value233 ); }
};

template <class T> struct Box233
{
    T m_Value;
    T m_Other;
    Box233() : m_Value(), m_Other() {}
    explicit Box233( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box233 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box233<int> IntBox233;
typedef Box233<double> DoubleBox233;
inline int Total233( const IntBox233 & a, const DoubleBox233 & b, const Record233 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 233 ) + (int)r.Mix( 233u );
}
static const int g_Table233[] = { 233, 466, 700, 1167, 1634, 2567 };

struct Record234
{
    int m_Id234;
    double m_Value234;
    char m_Name234[ 28 ];
    Record234 * m_Next;
    const char * m_Label;
    unsigned m_Flags3;
    Record234() : m_Id234( 234 ), m_Value234( 234.5 ), m_Next( nullptr ), m_Label( "record234" ), m_Flags3( 702u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name234 ); ++c ) { m_Name234[ c ] = (char)( 65 + ( ( c + 234 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id234 + a * 4 + b - (int)( m_Value234 * 5 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 265u ) ^ ( m_Flags3 << 0 ) ^ (unsigned)m_Id234; }
    bool Less( const Record234 & other ) const { return m_Id234 < other.m_Id234 || ( m_Id234 == other.m_Id234 && m_Value234 < other.m_Value234 ); }
};

template <class T> struct Box234
{
    T m_Value;
    T m_Other;
    Box234() : m_Value(), m_Other() {}
    explicit Box234( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box234 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box234<int> IntBox234;
typedef Box234<double> DoubleBox234;
inline int Total234( const IntBox234 & a, const DoubleBox234 & b, const Record234 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 234 ) + (int)r.Mix( 234u );
}
static const int g_Table234[] = { 234, 468, 703, 1172, 1641, 2578 };

struct Record235
{
    int m_Id235;
    double m_Value235;
    char m_Name235[ 12 ];
    Record235 * m_Next;
    const char * m_Label;
    unsigned m_Flags4;
    Record235() : m_Id235( 235 ), m_Value235( 235.5 ), m_Next( nullptr ), m_Label( "record235" ), m_Flags4( 705u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name235 ); ++c ) { m_Name235[ c ] = (char)( 65 + ( ( c + 235 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id235 + a * 5 + b - (int)( m_Value235 * 6 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 266u ) ^ ( m_Flags4 << 1 ) ^ (unsigned)m_Id235; }
    bool Less( const Record235 & other ) const { return m_Id235 < other.m_Id235 || ( m_Id235 == other.m_Id235 && m_Value235 < other.m_Value235 ); }
};

template <class T> struct Box235
{
    T m_Value;
    T m_Other;
    Box235() : m_Value(), m_Other() {}
    explicit Box235( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box235 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box235<int> IntBox235;
typedef Box235<double> DoubleBox235;
inline int Total235( const IntBox235 & a, const DoubleBox235 & b, const Record235 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 235 ) + (int)r.Mix( 235u );
}
static const int g_Table235[] = { 235, 470, 706, 1177, 1648, 2589 };

struct Record236
{
    int m_Id236;
    double m_Value236;
    char m_Name236[ 16 ];
    Record236 * m_Next;
    const char * m_Label;
    unsigned m_Flags5;
    Record236() : m_Id236( 236 ), m_Value236( 236.5 ), m_Next( nullptr ), m_Label( "record236" ), m_Flags5( 708u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name236 ); ++c ) { m_Name236[ c ] = (char)( 65 + ( ( c + 236 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id236 + a * 6 + b - (int)( m_Value236 * 7 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 267u ) ^ ( m_Flags5 << 2 ) ^ (unsigned)m_Id236; }
    bool Less( const Record236 & other ) const { return m_Id236 < other.m_Id236 || ( m_Id236 == other.m_Id236 && m_Value236 < other.m_Value236 ); }
};

template <class T> struct Box236
{
    T m_Value;
    T m_Other;
    Box236() : m_Value(), m_Other() {}
    explicit Box236( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box236 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box236<int> IntBox236;
typedef Box236<double> DoubleBox236;
inline int Total236( const IntBox236 & a, const DoubleBox236 & b, const Record236 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 236 ) + (int)r.Mix( 236u );
}
static const int g_Table236[] = { 236, 472, 709, 1182, 1655, 2600 };

struct Record237
{
    int m_Id237;
    double m_Value237;
    char m_Name237[ 20 ];
    Record237 * m_Next;
    const char * m_Label;
    unsigned m_Flags6;
    Record237() : m_Id237( 237 ), m_Value237( 237.5 ), m_Next( nullptr ), m_Label( "record237" ), m_Flags6( 711u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name237 ); ++c ) { m_Name237[ c ] = (char)( 65 + ( ( c + 237 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id237 + a * 7 + b - (int)( m_Value237 * 8 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 268u ) ^ ( m_Flags6 << 3 ) ^ (unsigned)m_Id237; }
    bool Less( const Record237 & other ) const { return m_Id237 < other.m_Id237 || ( m_Id237 == other.m_Id237 && m_Value237 < other.m_Value237 ); }
};

template <class T> struct Box237
{
    T m_Value;
    T m_Other;
    Box237() : m_Value(), m_Other() {}
    explicit Box237( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box237 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box237<int> IntBox237;
typedef Box237<double> DoubleBox237;
inline int Total237( const IntBox237 & a, const DoubleBox237 & b, const Record237 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 237 ) + (int)r.Mix( 237u );
}
static const int g_Table237[] = { 237, 474, 712, 1187, 1662, 2611 };

struct Record238
{
    int m_Id238;
    double m_Value238;
    char m_Name238[ 24 ];
    Record238 * m_Next;
    const char * m_Label;
    unsigned m_Flags7;
    Record238() : m_Id238( 238 ), m_Value238( 238.5 ), m_Next( nullptr ), m_Label( "record238" ), m_Flags7( 714u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name238 ); ++c ) { m_Name238[ c ] = (char)( 65 + ( ( c + 238 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id238 + a * 1 + b - (int)( m_Value238 * 2 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 269u ) ^ ( m_Flags7 << 4 ) ^ (unsigned)m_Id238; }
    bool Less( const Record238 & other ) const { return m_Id238 < other.m_Id238 || ( m_Id238 == other.m_Id238 && m_Value238 < other.m_Value238 ); }
};

template <class T> struct Box238
{
    T m_Value;
    T m_Other;
    Box238() : m_Value(), m_Other() {}
    explicit Box238( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box238 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box238<int> IntBox238;
typedef Box238<double> DoubleBox238;
inline int Total238( const IntBox238 & a, const DoubleBox238 & b, const Record238 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 238 ) + (int)r.Mix( 238u );
}
static const int g_Table238[] = { 238, 476, 715, 1192, 1669, 2622 };

struct Record239
{
    int m_Id239;
    double m_Value239;
    char m_Name239[ 28 ];
    Record239 * m_Next;
    const char * m_Label;
    unsigned m_Flags8;
    Record239() : m_Id239( 239 ), m_Value239( 239.5 ), m_Next( nullptr ), m_Label( "record239" ), m_Flags8( 717u )
    {
        for ( unsigned c = 0; c < sizeof( m_Name239 ); ++c ) { m_Name239[ c ] = (char)( 65 + ( ( c + 239 ) % 26 ) ); }
    }
    int Sum( int a, int b ) const { return m_Id239 + a * 2 + b - (int)( m_Value239 * 3 ); }
    unsigned Mix( unsigned seed ) const { return ( seed * 270u ) ^ ( m_Flags8 << 5 ) ^ (unsigned)m_Id239; }
    bool Less( const Record239 & other ) const { return m_Id239 < other.m_Id239 || ( m_Id239 == other.m_Id239 && m_Value239 < other.m_Value239 ); }
};

template <class T> struct Box239
{
    T m_Value;
    T m_Other;
    Box239() : m_Value(), m_Other() {}
    explicit Box239( const T & v ) : m_Value( v ), m_Other( v ) {}
    T Get() const { return m_Value; }
    void Set( const T & v ) { m_Other = m_Value; m_Value = v; }
    T Combine( const Box239 & b ) const { return (T)( m_Value + b.m_Other ); }
};
typedef Box239<int> IntBox239;
typedef Box239<double> DoubleBox239;
inline int Total239( const IntBox239 & a, const DoubleBox239 & b, const Record239 & r )
{
    return a.Combine( a ) + (int)b.Get() + r.Sum( a.Get(), 239 ) + (int)r.Mix( 239u );
}
static const int g_Table239[] = { 239, 478, 718, 1197, 1676, 2633 };

inline int AllTotals()
{
    int total = 0;
    total += Total0( IntBox0( 0 ), DoubleBox0( 0.0 ), Record0() );
    total += Total13( IntBox13( 13 ), DoubleBox13( 13.0 ), Record13() );
    total += Total26( IntBox26( 26 ), DoubleBox26( 26.0 ), Record26() );
    total += Total39( IntBox39( 39 ), DoubleBox39( 39.0 ), Record39() );
    total += Total52( IntBox52( 52 ), DoubleBox52( 52.0 ), Record52() );
    total += Total65( IntBox65( 65 ), DoubleBox65( 65.0 ), Record65() );
    total += Total78( IntBox78( 78 ), DoubleBox78( 78.0 ), Record78() );
    total += Total91( IntBox91( 91 ), DoubleBox91( 91.0 ), Record91() );
    total += Total104( IntBox104( 104 ), DoubleBox104( 104.0 ), Record104() );
    total += Total117( IntBox117( 117 ), DoubleBox117( 117.0 ), Record117() );
    total += Total130( IntBox130( 130 ), DoubleBox130( 130.0 ), Record130() );
    total += Total143( IntBox143( 143 ), DoubleBox143( 143.0 ), Record143() );
    total += Total156( IntBox156( 156 ), DoubleBox156( 156.0 ), Record156() );
    total += Total169( IntBox169( 169 ), DoubleBox169( 169.0 ), Record169() );
    total += Total182( IntBox182( 182 ), DoubleBox182( 182.0 ), Record182() );
    total += Total195( IntBox195( 195 ), DoubleBox195( 195.0 ), Record195() );
    total += Total208( IntBox208( 208 ), DoubleBox208( 208.0 ), Record208() );
    total += Total221( IntBox221( 221 ), DoubleBox221( 221.0 ), Record221() );
    total += Total234( IntBox234( 234 ), DoubleBox234( 234.0 ), Record234() );
    return total;
}

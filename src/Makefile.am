AUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS = @BOOST_CPPFLAGS@ -I$(srcdir) \
        -DBOOST_ASIO_DISABLE_VISIBILITY \
        -DBOOST_BIND_GLOBAL_PLACEHOLDERS \
        -DBOOST_THREAD_VERSION=4
AM_LDFLAGS = @BOOST_LDFLAGS@
LDADD = -lboost_system$(BOOST_LIB_SUFFIX) \
        -lboost_thread$(BOOST_LIB_SUFFIX) \
        -lboost_coroutine$(BOOST_LIB_SUFFIX) \
        -lboost_chrono$(BOOST_LIB_SUFFIX)

bin_PROGRAMS = file_saver_server file_saver_client
file_saver_server_SOURCES = file_saver_server.cpp
file_saver_client_SOURCES = file_saver_client.cpp
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/bind/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <syslog.h>
#include <iostream>

static volatile int sighup_reset = 0;

class session : public boost::enable_shared_from_this<session>
{
public:
  explicit session(boost::asio::io_context &io_context)
      : strand_(boost::asio::make_strand(io_context)),
        socket_(io_context),
        timer_(io_context)
  {
  }

  boost::asio::ip::tcp::socket &socket()
  {
    return socket_;
  }

  void go()
  {
    boost::asio::spawn(strand_,
                       boost::bind(&session::do_read,
                                   shared_from_this(), boost::placeholders::_1));
    boost::asio::spawn(strand_,
                       boost::bind(&session::do_timeout,
                                   shared_from_this(), boost::placeholders::_1));
  }

private:
  void do_read(boost::asio::yield_context yield)
  {
    struct on_exit
    {
      boost::asio::ip::tcp::socket &socket;
      boost::asio::steady_timer &timer;
      int file_descriptor;

      ~on_exit()
      {
        socket.close();
        timer.cancel();
        ::close(file_descriptor);
      }
    };

    std::string data;
    on_exit on_exit_ = {socket_, timer_, -1};

    try
    {
      timer_.expires_after(boost::asio::chrono::seconds(10));
      std::size_t n = boost::asio::async_read_until(
          socket_, boost::asio::dynamic_buffer(data), '\n', yield);

      std::string file_name;
      std::size_t file_size = 0;
      {
        std::istringstream is(data);
        if (!(is >> std::hex >> file_size))
          boost::throw_exception(
              boost::system::system_error(
                  boost::asio::error::invalid_argument));

        std::getline(is, file_name);
        file_name.erase(0, 1);
      }

      boost::asio::dynamic_buffer(data).consume(n);

      timer_.expires_at(boost::asio::steady_timer::time_point::max());

      if ((on_exit_.file_descriptor = ::open(
               file_name.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
               S_IRUSR | S_IWUSR)) < 0)
        boost::throw_exception(
            boost::system::system_error(
                errno, boost::system::system_category()));

      n = data.size();
      for (;;)
      {
        if (::write(on_exit_.file_descriptor, data.c_str(), n) != n)
          boost::throw_exception(
              boost::system::system_error(
                  boost::asio::error::interrupted));

        boost::asio::dynamic_buffer(data).consume(n);

        if ((file_size -= n) == 0)
          break;

        boost::asio::dynamic_buffer(data).grow(256);

        timer_.expires_after(boost::asio::chrono::seconds(10));
        n = socket_.async_read_some(boost::asio::buffer(data), yield);
      }
    }
    catch (std::exception const &)
    {
      // ...
    }
  }

  void do_timeout(boost::asio::yield_context yield)
  {
    while (socket_.is_open())
    {
      boost::system::error_code ignored_ec;
      timer_.async_wait(yield[ignored_ec]);
      if (timer_.expiry() <= boost::asio::steady_timer::clock_type::now())
        socket_.close();
    }
  }

  boost::asio::strand<boost::asio::io_context::executor_type> strand_;
  boost::asio::ip::tcp::socket socket_;
  boost::asio::steady_timer timer_;
};

void do_handle_signal(
    boost::asio::io_context &io_context,
    boost::asio::ip::tcp::acceptor &acceptor,
    boost::asio::yield_context yield)
{
  boost::asio::signal_set signals(io_context);
  signals.add(SIGINT);
  signals.add(SIGTERM);
#if defined(SIGQUIT)
  signals.add(SIGQUIT);
#endif // defined(SIGQUIT)
  signals.add(SIGHUP);

  for (;;)
  {
    switch (signals.async_wait(yield))
    {
    case SIGINT:
    case SIGTERM:
#if defined(SIGQUIT)
    case SIGQUIT:
#endif // defined(SIGQUIT)
      io_context.stop();
      break;

    case SIGHUP:
      acceptor.cancel();
      sighup_reset = 1;
      return;

    default:
      BOOST_ASSERT(0);
    }
  }
}

void do_accept(
    boost::asio::io_context &io_context,
    const boost::asio::ip::tcp::endpoint &endpoint,
    boost::asio::yield_context yield)
{
  boost::asio::ip::tcp::acceptor acceptor(io_context, endpoint);

  boost::asio::spawn(
      io_context, boost::bind(
                      do_handle_signal, boost::ref(io_context),
                      boost::ref(acceptor), boost::placeholders::_1));

  for (;;)
  {
    boost::system::error_code ec;
    boost::shared_ptr<session> new_session(new session(io_context));
    acceptor.async_accept(new_session->socket(), yield[ec]);

    if (ec == boost::asio::error::operation_aborted && sighup_reset)
    {
      // Check conf...
      sighup_reset = 0;
      boost::asio::spawn(
          io_context, boost::bind(
                          do_accept, boost::ref(io_context),
                          endpoint, boost::placeholders::_1));
      return;
    }

    if (!ec)
      new_session->go();
  }
}

int main(int argc, char *argv[])
{
  try
  {
    if (argc != 3)
    {
      std::cerr << "Usage: file_saver_server <address> <port>\n";
      return 1;
    }

    boost::asio::io_context io_context;

    boost::asio::spawn(
        io_context,
        boost::bind(
            do_accept, boost::ref(io_context),
            boost::asio::ip::tcp::endpoint(
                boost::asio::ip::make_address(argv[1]), atoi(argv[2])),
            boost::placeholders::_1));

    io_context.notify_fork(boost::asio::io_context::fork_prepare);

    // Switch to background process
    if (pid_t pid = ::fork())
    {
      if (pid > 0)
        exit(0);
      else
      {
        syslog(LOG_ERR | LOG_USER, "First fork failed: %m");
        return 1;
      }
    }

    // Become leader of new session
    ::setsid();

    // Change work dir to root dir
    //::chdir("/");

    // Clear file mode creation mask
    ::umask(0);

    // Ensure we are not acquire controlling terminal
    if (pid_t pid = ::fork())
    {
      if (pid > 0)
        ::exit(0);
      else
      {
        ::syslog(LOG_ERR | LOG_USER, "Second fork failed: %m");
        return 1;
      }
    }

    // Close standard descriptors
    ::close(STDIN_FILENO);
    ::close(STDOUT_FILENO);
    ::close(STDERR_FILENO);

    // Send standard input to null
    if (::open("/dev/null", O_RDONLY) < 0)
    {
      ::syslog(LOG_ERR | LOG_USER, "Unable to open /dev/null: %m");
      return 1;
    }

    // Send standard output to log
    const char *output = "/tmp/file_saver_server.daemon.out";
    const int flags = O_WRONLY | O_CREAT | O_APPEND;
    const mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    if (::open(output, flags, mode) < 0)
    {
      ::syslog(LOG_ERR | LOG_USER, "Unable to open output file %s: %m", output);
      return 1;
    }

    // Also send standard error to the same log file.
    if (::dup(1) < 0)
    {
      ::syslog(LOG_ERR | LOG_USER, "Unable to dup output descriptor: %m");
      return 1;
    }

    io_context.notify_fork(boost::asio::io_context::fork_child);

    // The io_context can now be used normally.
    ::syslog(LOG_USER | LOG_USER, "Daemon started");
    io_context.run();
    ::syslog(LOG_USER | LOG_USER, "Daemon stopped");
  }
  catch (std::exception &e)
  {
    ::syslog(LOG_ERR | LOG_USER, "Exception: %s", e.what());
    std::cerr << "Exception: " << e.what() << std::endl;
  }

  return EXIT_SUCCESS;
}
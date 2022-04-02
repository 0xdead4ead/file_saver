#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/strand.hpp>
#include <boost/bind/bind.hpp>
#include <iostream>

void do_connect_timeout(
    boost::asio::ip::tcp::socket &socket,
    boost::asio::steady_timer &timer,
    boost::asio::yield_context yield)
{
    timer.expires_after(boost::asio::chrono::seconds(10));

    boost::system::error_code ignored_ec;
    timer.async_wait(yield[ignored_ec]);
    if (timer.expiry() <= boost::asio::steady_timer::clock_type::now())
        socket.close();
}

void do_write(boost::asio::io_context &io_context,
              const boost::asio::ip::tcp::endpoint &endpoint, const char *file_path,
              boost::asio::yield_context yield)
{
    int file_descriptor = -1;
    boost::asio::ip::tcp::socket socket(io_context);
    boost::asio::steady_timer timer(io_context);

    boost::asio::spawn(
        yield, boost::bind(
                    do_connect_timeout,
                    boost::ref(socket), boost::ref(timer),
                    boost::placeholders::_1));

    socket.async_connect(endpoint, yield);
    timer.cancel();

    if ((file_descriptor = ::open(file_path, O_RDONLY)) < 0)
        boost::throw_exception(
            boost::system::system_error(
                errno, boost::system::system_category()));

    std::size_t block_size;
    std::size_t file_size;
    {
        struct stat fi;
        if (fstat(file_descriptor, &fi) < 0)
            boost::throw_exception(
                boost::system::system_error(
                    errno, boost::system::system_category()));

        block_size = fi.st_blksize;
        file_size = fi.st_size;
    }

    const char *file_name = file_path;
    while ((file_path = std::strchr(file_path, '/')) != 0)
    {
        file_path++;
        file_name = file_path;
    }

    {
        std::ostringstream header_stream;
        header_stream << std::hex << file_size;
        header_stream << ' ' << file_name << std::endl;

        std::string header = header_stream.str(); // copy required here
        boost::asio::async_write(socket, boost::asio::buffer(header), yield);
    }

    ssize_t n;
    std::string buffer;
    buffer.reserve(block_size);
    while ((n = ::read(file_descriptor, &buffer[0], block_size)) > 0)
    {
        boost::asio::async_write(socket, boost::asio::buffer(buffer.data(), n), yield);
    }

    if (n < 0)
        boost::throw_exception(
            boost::system::system_error(
                errno, boost::system::system_category()));
}

int main(int argc, char *argv[])
{
    try
    {
        if (argc != 4)
        {
            std::cerr << "Usage: file_saver_client <filepath> <address> <port>\n";
            return 1;
        }

        boost::asio::io_context io_context;

        boost::asio::spawn(
            io_context,
            boost::bind(
                do_write, boost::ref(io_context),
                boost::asio::ip::tcp::endpoint(
                    boost::asio::ip::make_address(argv[2]),
                    atoi(argv[3])),
                argv[1],
                boost::placeholders::_1));

        io_context.run();
    }
    catch (std::exception &e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return EXIT_SUCCESS;
}
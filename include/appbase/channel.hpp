#pragma once

//clashes with BOOST PP and Some Applications
#pragma push_macro("N")
#undef N

#include <boost/asio.hpp>
#include <boost/signals2.hpp>
#include <boost/exception/diagnostic_information.hpp>

namespace appbase {
   // std::unique_ptr<void>可以存放任意类型的指针(类似void*)
   // 实际上是一个channel*的wrapper，由application::channels进行统一管理。
   using erased_channel_ptr = std::unique_ptr<void, void(*)(void*)>;

   /**
    * A basic DispatchPolicy that will catch and drop any exceptions thrown
    * during the dispatch of messages on a channel
    */
   struct drop_exceptions {
      drop_exceptions() = default;
      using result_type = void;

      template<typename InputIterator>
      result_type operator()(InputIterator first, InputIterator last) {
         while (first != last) {
            try {
               *first;
            } catch (...) {
               // drop
            }
            ++first;
         }
      }
   };

   /**
    * A channel is a loosely bound asynchronous data pub/sub concept.
    *
    * This removes the need to tightly couple different plugins in the application for the use-case of
    * sending data around
    *
    * Data passed to a channel is *copied*, consider using a shared_ptr if the use-case allows it
    *
    * 用法：
    *   1. 创建一个channel
    *   2.
    * @tparam Data - the type of data to publish
    *
    */
   template<typename Data, typename DispatchPolicy>
   class channel final {
      public:
         /**
          * Type that represents an active subscription to a channel allowing
          * for ownership via RAII and also explicit unsubscribe actions
          */
         class handle {
            //一个handle封装signal::connection, signal::connection中封装一个注册的函数
            //一个channel封装一个signal，一个signal上可以注册多个回调函数
            //所以：一个channel 上可以有多个handle
            public:
               ~handle() {
                  unsubscribe();
               }

               /**
                * Explicitly unsubcribe from channel before the lifetime
                * of this object expires
                */
               void unsubscribe() {

                  if (_handle.connected()) {
                     _handle.disconnect();
                  }
               }

               // This handle can be constructed and moved
               handle() = default;
               handle(handle&&) = default;
               handle& operator= (handle&& rhs) = default;

               // dont allow copying since this protects the resource
               handle(const handle& ) = delete;
               handle& operator= (const handle& ) = delete;

            private:
               using handle_type = boost::signals2::connection;
               handle_type _handle;

               /**
                * Construct a handle from an internal represenation of a handle
                * In this case a boost::signals2::connection
                *
                * @param _handle - the boost::signals2::connection to wrap
                */
               handle(handle_type&& _handle)
               :_handle(std::move(_handle))
               {}

               friend class channel;
         };

         /**
          * Publish data to a channel.  This data is *copied* on publish.
          * @param priority - the priority to use for post
          * @param data - the data to publish
          */
         void publish(int priority, const Data& data);

         /**
          * subscribe to data on a channel
          * @tparam Callback the type of the callback (functor|lambda)
          * @param cb the callback
          * @return handle to the subscription
          */
         template<typename Callback>
         handle subscribe(Callback cb) {
             // _signal.connect 用于将注册监听signal的函数，返回signal::connection
             // 通过signal::connection::connected()的返回值来判断注册的函数是否被调用
            return handle(_signal.connect(cb));
         }

         /**
          * set the dispatcher according to the DispatchPolicy
          * this can be used to set a stateful dispatcher
          *
          * This method is only available when the DispatchPolicy is copy constructible due to implementation details
          *
          * @param policy - the DispatchPolicy to copy
          */
         auto set_dispatcher(const DispatchPolicy& policy ) -> std::enable_if_t<std::is_copy_constructible<DispatchPolicy>::value,void>
         {
            _signal.set_combiner(policy);
         }

         /**
          * Returns whether or not there are subscribers
          */
         bool has_subscribers() {
            auto connections = _signal.num_slots();
            return connections > 0;
         }

      private:
         channel()
         {
         }

         virtual ~channel() = default;

         /**
          * Proper deleter for type-erased channel
          * note: no type checking is performed at this level
          *
          * @param erased_channel_ptr
          */
         static void deleter(void* erased_channel_ptr) {
            auto ptr = reinterpret_cast<channel*>(erased_channel_ptr);
            delete ptr;
         }

         /**
          * get the channel back from an erased pointer
          *
          * @param ptr - the type-erased channel pointer
          * @return - the type safe channel pointer
          */
         static channel* get_channel(erased_channel_ptr& ptr) {
            return reinterpret_cast<channel*>(ptr.get());
         }

         /**
          * Construct a unique_ptr for the type erased method poiner
          * @return
          */
         static erased_channel_ptr make_unique()
         {
            return erased_channel_ptr(new channel(), &deleter);
         }

         //boost::signals2::signal模板有两个参数，第一个是注册到这个信号上的函数的类型
         boost::signals2::signal<void(const Data&), DispatchPolicy> _signal;

         friend class appbase::application;
   };

   /**
    *
    * @tparam Tag - API specific discriminator used to distinguish between otherwise identical data types
    * Tag参数实际上并不是真正需要定义的struct,仅仅是拿来当作一个标识使用的
    * @tparam Data - the typ of the Data the channel carries
    * @tparam DispatchPolicy - The dispatch policy to use for this channel (defaults to @ref drop_exceptions)
    */
   template< typename Tag, typename Data, typename DispatchPolicy = drop_exceptions >
   struct channel_decl {
      using channel_type = channel<Data, DispatchPolicy>;
      using tag_type = Tag;
   };

   template <typename...Ts>
   std::true_type is_channel_decl_impl(const channel_decl<Ts...>*);

   std::false_type is_channel_decl_impl(...);

   template <typename T>
   using is_channel_decl = decltype(is_channel_decl_impl(std::declval<T*>()));
}

#pragma pop_macro("N")

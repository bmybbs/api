这部分为 API 的自动化测试代码。使用中需要：

1. 先准备测试数据，包括用户、版面
2. 安装 node
3. 执行 `sudo npm install -g nodeunit` 安装 [nodeunit](https://github.com/caolan/nodeunit) ，一款 JavaScript 的单元测试框架。
4. 执行 `sudo npm install -g jquery` 安装 jQuery 的 node 包

测试的时候依次使用 `nodeunit` 调用每个 js，例如

```bash
$ nodeunit test_article.js

test_article.js
✔ test_get_top_10_article_list
✔ test_get_announce_article_list_if_announce_exist
✔ test_get_commend_article_list_if_commend_exist
✔ test_get_the_second_annouce_entry_with_startnum_and_count
✔ test_get_the_second_commend_entry_with_startnum_and_count

OK: 12 assertions (9553ms)
```

说明 article 接口 **已有** 的测试用例已经通过校验。

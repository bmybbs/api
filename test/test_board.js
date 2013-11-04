/**
 * @file 版面列表接口自动化测试
 * @author IronBlood
 * 该用例目前用于读取 test 用户的收藏夹列表。该列表中存在 sysop 和 XJTUnews 两个版面，其中前者存在未读信息，后者已读。
 *
 * @warning 新增测试用例前请补充测试场景、测试数据说明。
 */

var $ = require('jquery');

exports.test_get_fav_board_list = function(test) {
	var login_url = 'http://extdev.ironblood.net:8080/user/login?userid=test&passwd=testtest&appkey=newweb';
	$.getJSON(login_url, function(login_data) {
		if(login_data.errcode == 0) {
			var fav_url = 'http://extdev.ironblood.net:8080/board/list?secstr=fav&userid=test&sessid='+login_data.SessionID+'&appkey=newweb&sortmode=1';
			$.getJSON(fav_url, function(fav_data) {
				//console.log(fav_url);
				if(fav_data.errcode == 0) {
					test.equal(fav_data.errcode, 0, "errcode 错误，预期 0，实际 " + fav_data.errcode);
					test.equal(fav_data.boardlist.length, 2, "数组长度错误，预期2，实际 " + fav_data.boardlist.length);

					// sysop 版面
					test.equal(fav_data.boardlist[0].name, "sysop");
					test.equal(fav_data.boardlist[0].unread, 1);

					// XJTUnews 版面
					test.equal(fav_data.boardlist[1].name, "XJTUnews");
					test.equal(fav_data.boardlist[1].unread, 0);
					test.done();
				} else {
					test.ok(false, "get fav list failed. errcode: " + fav_data.errcode);
					test.done();
				}
			});
		} else {
			test.ok(false, "user login failed. errcode: " + login_data.errcode);
			test.done();
		}
	});
};

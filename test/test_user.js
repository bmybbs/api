var $ = require('jquery');

exports.test_user_login_test_with_correct_password = function(test) {
	var url = 'http://extdev.ironblood.net:8080/user/login?userid=test&passwd=testtest&appkey=newweb';
	$.getJSON(url, function(data) {
		if(data.errcode == 0) {
			test.ok(true, "user test login successully.");
//			console.log(data);
			test.done();
		} else {
			test.ok(false, "user test login failed. errcode: " + data.errcode);
			test.done();
		}
	})
};
